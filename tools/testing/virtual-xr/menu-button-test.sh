#!/usr/bin/env bash
set -uo pipefail
. "$(cd "$(dirname "$0")" && pwd)/common.sh"
SCRIPT="$TOOL_DIR/launch-virtual-xr.sh"
FIFO="$BASE/menu-button.stdin.fifo"
SUM="$BASE/menu-button-summary.txt"
PID=""; FD_OPEN=0
cleanup(){ if [ -n "${PID:-}" ] && kill -0 "$PID" 2>/dev/null; then kill -TERM "$PID" 2>/dev/null || true; sleep .3; kill -KILL "$PID" 2>/dev/null || true; fi; if [ "${FD_OPEN:-0}" = 1 ]; then exec 8>&- 2>/dev/null || true; fi; rm -f "$FIFO"; }
trap cleanup EXIT INT TERM
: > "$SUM"
"$SCRIPT" start >/dev/null 2>&1 || exit 1
ID=$(wmctrl -l 2>/dev/null | awk '/Monado!/ {print $1; exit}')
[ -n "$ID" ] || { echo 'RESULT=blocked no_window' | tee -a "$SUM"; exit 1; }
for key in n b; do
  TRACE="$BASE/menu-button-$key.trace.log"; XRL="$BASE/menu-button-$key.hello-xr.log"
  rm -f "$TRACE" "$XRL" "$FIFO"; mkfifo "$FIFO"; exec 8<>"$FIFO"; FD_OPEN=1
  env XDG_RUNTIME_DIR="$RUNTIME_DIR" DISPLAY="$DISPLAY" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" XR_RUNTIME_JSON="$RUNTIME_JSON" LD_PRELOAD="$SHIM" XR_INPUT_TRACE_LOG="$TRACE" timeout --signal=TERM --kill-after=5s 20 "$HELLO_XR" -G OpenGL <&8 >"$XRL" 2>&1 &
  PID=$!
  for _ in $(seq 1 100); do sleep .1; rg -q 'SESSION_STATE state=5' "$TRACE" 2>/dev/null && break; kill -0 "$PID" 2>/dev/null || break; done
  sleep .3
  wmctrl -ia "$ID" >/dev/null 2>&1 || true; xdotool windowactivate --sync "$ID" >/dev/null 2>&1 || true
  START=$(wc -l < "$TRACE")
  xdotool keydown --window "$ID" "$key" >/dev/null 2>&1 || true
  sleep 1.2
  xdotool keyup --window "$ID" "$key" >/dev/null 2>&1 || true
  sleep .8
  ALIVE=yes; kill -0 "$PID" 2>/dev/null || ALIVE=no
  echo "key=$key alive_after_press=$ALIVE" | tee -a "$SUM"
  echo '--- bool/action slice ---' | tee -a "$SUM"
  tail -n +$((START+1)) "$TRACE" | rg 'ACTION_BOOL|ACTION_FLOAT|SESSION_STATE' | tee -a "$SUM" || true
  printf '\n' >&8 2>/dev/null || true
  for _ in $(seq 1 30); do kill -0 "$PID" 2>/dev/null || break; sleep .1; done
  kill -TERM "$PID" 2>/dev/null || true
  set +e; wait "$PID" 2>/dev/null; echo "key=$key exit_rc=$?" | tee -a "$SUM"; set -e
  exec 8>&- 2>/dev/null || true; FD_OPEN=0; rm -f "$FIFO"; sleep .4
done
