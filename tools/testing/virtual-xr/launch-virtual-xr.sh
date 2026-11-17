#!/usr/bin/env bash
# Persistent launcher for an isolated Monado QWERTY OpenXR runtime.
#
# The runtime is selected per application with XR_RUNTIME_JSON and a private
# XDG_RUNTIME_DIR. This script never edits active_runtime.json, never starts
# WiVRn, and never stops a service it did not start.
#
# Required:
#   VENTURE_APPIMAGE=/path/to/Venture.AppImage
# Optional:
#   VENTURE_APPIMAGE_SHA256=<expected sha256>
#   VENTURE_VIRTUAL_BASE=<data directory>  (default: $XDG_DATA_HOME/venture-virtual-hmd)
#   VENTURE_MONADO_JSON=<runtime manifest>
#   VENTURE_MONADO_SERVICE=<monado-service binary>
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE="${VENTURE_VIRTUAL_BASE:-${XDG_DATA_HOME:-$HOME/.local/share}/venture-virtual-hmd}"
RUNTIME_DIR="${VENTURE_VIRTUAL_RUNTIME_DIR:-$BASE/runtime}"
APPIMAGE="${VENTURE_APPIMAGE:-}"
EXPECTED_SHA="${VENTURE_APPIMAGE_SHA256:-}"
MONADO_JSON="${VENTURE_MONADO_JSON:-/usr/share/openxr/1/openxr_monado.json}"
SERVICE_LOG="${VENTURE_SERVICE_LOG:-$BASE/monado-service.log}"
PIDFILE="$BASE/monado-service.pid"
SOCKET="$RUNTIME_DIR/monado_comp_ipc"
SERVICE_BIN="${VENTURE_MONADO_SERVICE:-/usr/bin/monado-service}"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*" >&2; }

usage() {
  cat <<EOF
Usage: $0 COMMAND

Commands:
  start              Start the isolated persistent Monado QWERTY service.
  status             Show this script's service, private runtime, and GUI.
  test               Run five consecutive normal hello_xr sessions against
                     the one running service. The service is left running.
  input-test         Run the verified head and left/right controller test
                     against the one running service. The service is left
                     running.
  game PACKAGE       Run VENTURE_APPIMAGE for PACKAGE using this service.
                     The service is left running for the next game.
                     Set VENTURE_GAME_TIMEOUT=SECONDS to bound the run.
  stop               Stop only the service recorded by this script's pidfile.
  env                Print the per-application environment exports.
  help               This text.

Required environment:
  VENTURE_APPIMAGE=/absolute/path/to/Venture.AppImage

Runtime selection is per application:
  XR_RUNTIME_JSON=$MONADO_JSON
  XDG_RUNTIME_DIR=$RUNTIME_DIR
The system-wide OpenXR registration and WiVRn are not modified.

Important service options:
  XRT_NO_STDIN=1 keeps monado-service from polling stdin. Standalone
  monado-service otherwise treats stdin readiness/HUP as a shutdown request.
EOF
}

check_appimage() {
  [ -n "$APPIMAGE" ] || die "set VENTURE_APPIMAGE to the candidate AppImage"
  [ -f "$APPIMAGE" ] || die "candidate not found: $APPIMAGE"
  [ -x "$APPIMAGE" ] || die "candidate is not executable: $APPIMAGE"
  local actual
  actual=$(sha256sum "$APPIMAGE" | awk '{print $1}')
  if [ -n "$EXPECTED_SHA" ]; then
    [ "$actual" = "$EXPECTED_SHA" ] || \
      die "candidate hash mismatch: expected $EXPECTED_SHA, got $actual"
  else
    say "warning: VENTURE_APPIMAGE_SHA256 is unset; candidate hash is $actual"
  fi
}

check_runtime_json() {
  [ -f "$MONADO_JSON" ] || die "Monado runtime JSON not found: $MONADO_JSON"
}

prepare_runtime_dir() {
  mkdir -p "$RUNTIME_DIR" "$BASE"
  local session_dir="/run/user/$(id -u)"
  local name target link_target
  for name in wayland-1 wayland-1.lock bus pulse; do
    target="$session_dir/$name"
    link_target="$RUNTIME_DIR/$name"
    if [ -e "$target" ] && [ ! -e "$link_target" ] && [ ! -L "$link_target" ]; then
      [ "$(readlink -f "$target" 2>/dev/null)" = "$(readlink -f "$RUNTIME_DIR" 2>/dev/null)/$name" ] && continue
      ln -s "$target" "$link_target" 2>/dev/null || true
    fi
  done
}

service_pid_alive() {
  local pid="${1:-}"
  [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

service_pid_owned() {
  local pid="${1:-}" comm
  service_pid_alive "$pid" || return 1
  comm=$(ps -o comm= -p "$pid" 2>/dev/null | tr -d '[:space:]')
  [ "$comm" = "monado-service" ]
}

socket_listener() {
  [ -S "$SOCKET" ] || return 1
  ss -xl 2>/dev/null | grep -F -- "$SOCKET" >/dev/null 2>&1
}

wait_for_service() {
  local pid="$1" i
  for i in $(seq 1 200); do
    if ! service_pid_alive "$pid"; then
      return 1
    fi
    if [ -S "$SOCKET" ] && socket_listener; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

wait_for_gui() {
  local i
  for i in $(seq 1 60); do
    if wmctrl -l 2>/dev/null | grep -q 'Monado!'; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

stop_service() {
  local pid="${1:-}" comm
  if [ -z "$pid" ] && [ -f "$PIDFILE" ]; then
    pid=$(cat "$PIDFILE" 2>/dev/null || true)
  fi
  if ! service_pid_alive "$pid"; then
    rm -f "$PIDFILE"
    rm -f "$RUNTIME_DIR/monado.pid"
    if [ -S "$SOCKET" ] && ! socket_listener; then
      say "removing stale private socket $SOCKET"
      rm -f "$SOCKET"
    fi
    return 0
  fi
  if ! service_pid_owned "$pid"; then
    say "refusing to stop PID $pid: it is not a monado-service process"
    return 1
  fi
  comm=$(ps -o comm= -p "$pid" 2>/dev/null | tr -d '[:space:]')
  say "stopping isolated monado-service PID $pid ($comm)"
  kill -TERM "$pid" 2>/dev/null || true
  for _ in $(seq 1 50); do
    service_pid_alive "$pid" || break
    sleep 0.1
  done
  if service_pid_alive "$pid"; then
    kill -KILL "$pid" 2>/dev/null || true
  fi
  rm -f "$PIDFILE"
  rm -f "$RUNTIME_DIR/monado.pid"
  if [ -S "$SOCKET" ] && ! socket_listener; then
    rm -f "$SOCKET"
  fi
}

start_service() {
  prepare_runtime_dir
  check_runtime_json
  [ -x "$SERVICE_BIN" ] || die "missing $SERVICE_BIN"

  local pid=""
  if [ -f "$PIDFILE" ]; then
    pid=$(cat "$PIDFILE" 2>/dev/null || true)
  fi

  if service_pid_owned "$pid"; then
    if [ -S "$SOCKET" ] && socket_listener; then
      say "isolated Monado already running (PID $pid, socket $SOCKET)"
      return 0
    fi
    say "owned service PID $pid has no live socket; stopping it before restart"
    stop_service "$pid" || true
    pid=""
  else
    if [ -n "$pid" ]; then
      say "removing stale pidfile PID $pid"
      rm -f "$PIDFILE"
    fi
  fi

  if [ -S "$SOCKET" ]; then
    if socket_listener; then
      die "private socket has an unknown listener: $SOCKET; refusing to compete"
    fi
    say "removing stale private socket $SOCKET"
    rm -f "$SOCKET"
  fi
  rm -f "$RUNTIME_DIR/monado.pid"

  say "starting isolated Monado QWERTY runtime; log: $SERVICE_LOG"
  : > "$SERVICE_LOG"
  nohup env \
    XDG_RUNTIME_DIR="$RUNTIME_DIR" \
    DISPLAY="${DISPLAY:-:0}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}" \
    SDL_VIDEODRIVER=x11 \
    QWERTY_ENABLE=1 \
    QWERTY_COMBINE=1 \
    XRT_DEBUG_GUI=1 \
    XRT_COMPOSITOR_FORCE_XCB=1 \
    XRT_COMPOSITOR_LOG=info \
    XRT_NO_STDIN=1 \
    IPC_EXIT_ON_DISCONNECT=0 \
    IPC_EXIT_WHEN_IDLE=0 \
    "$SERVICE_BIN" </dev/null >>"$SERVICE_LOG" 2>&1 &
  pid=$!
  echo "$pid" > "$PIDFILE"

  if ! wait_for_service "$pid"; then
    say "Monado did not become ready; tail of $SERVICE_LOG:"
    tail -40 "$SERVICE_LOG" >&2 || true
    rm -f "$PIDFILE"
    die "failed to start isolated Monado"
  fi
  if ! wait_for_gui; then
    say "warning: Monado service is running but its QWERTY GUI window was not found"
  fi
  say "isolated Monado ready (PID $pid)"
}

runtime_env() {
  export XDG_RUNTIME_DIR="$RUNTIME_DIR"
  export DISPLAY="${DISPLAY:-:0}"
  export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
  export XR_RUNTIME_JSON="$MONADO_JSON"
}

print_runtime() {
  local pid=""
  [ -f "$PIDFILE" ] && pid=$(cat "$PIDFILE" 2>/dev/null || true)
  say "active OpenXR runtime: $MONADO_JSON"
  say "private XDG_RUNTIME_DIR: $RUNTIME_DIR"
  say "isolated service PID: ${pid:-not-running}"
  if wmctrl -l 2>/dev/null | grep -q 'Monado!'; then
    say "QWERTY/debug GUI: present"
  else
    say "QWERTY/debug GUI: not found"
  fi
}

status() {
  local pid=""
  [ -f "$PIDFILE" ] && pid=$(cat "$PIDFILE" 2>/dev/null || true)
  if service_pid_owned "$pid"; then
    printf 'isolated Monado running: PID %s\n' "$pid"
    printf 'socket: %s\n' "$SOCKET"
    printf 'runtime JSON: %s\n' "$MONADO_JSON"
    printf 'private XDG_RUNTIME_DIR: %s\n' "$RUNTIME_DIR"
    printf 'log: %s\n' "$SERVICE_LOG"
    if socket_listener; then
      printf 'socket listener: yes\n'
    else
      printf 'socket listener: no (unexpected for a running service)\n'
    fi
    if wmctrl -l 2>/dev/null | grep -q 'Monado!'; then
      printf 'QWERTY GUI: present\n'
    else
      printf 'QWERTY GUI: not found\n'
    fi
  else
    printf 'isolated Monado not running under this script\n'
    [ -S "$SOCKET" ] && printf 'note: a socket exists at %s (not script-owned)\n' "$SOCKET" || true
  fi
}

print_env() {
  check_runtime_json
  cat <<EOF
export XDG_RUNTIME_DIR=$RUNTIME_DIR
export DISPLAY=${DISPLAY:-:0}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}
export XR_RUNTIME_JSON=$MONADO_JSON
# Then run the candidate:
# ${APPIMAGE:-<VENTURE_APPIMAGE>} --launch <package>
EOF
}

run_test() {
  local script="$BASE/session-cycle.sh"
  [ -x "$script" ] || die "missing session test: $script (copy it into $BASE)"
  "$script"
}

run_input_test() {
  local script="$BASE/controller-input-test.sh"
  [ -x "$script" ] || die "missing input test: $script (copy it into $BASE)"
  M35_BASE="$BASE" "$script"
}

run_game() {
  local pkg="${1:-}"
  [ -n "$pkg" ] || die "usage: $0 game PACKAGE"
  check_appimage
  start_service
  runtime_env
  print_runtime
  say "launching $pkg"
  set +e
  if [ -n "${VENTURE_GAME_TIMEOUT:-}" ]; then
    timeout --signal=TERM --kill-after=8s "$VENTURE_GAME_TIMEOUT" \
      "$APPIMAGE" --launch "$pkg"
  else
    "$APPIMAGE" --launch "$pkg"
  fi
  local rc=$?
  set -e
  say "game exit=$rc; isolated service is left running for the next launch"
  print_runtime
  return "$rc"
}

case "${1:-help}" in
  start)
    start_service
    ;;
  stop)
    pid=""
    [ -f "$PIDFILE" ] && pid=$(cat "$PIDFILE" 2>/dev/null || true)
    stop_service "$pid" || exit 1
    ;;
  status)
    status
    ;;
  test)
    start_service
    print_runtime
    run_test
    ;;
  input-test)
    start_service
    print_runtime
    run_input_test
    ;;
  game)
    run_game "${2:-}"
    ;;
  env)
    print_env
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
