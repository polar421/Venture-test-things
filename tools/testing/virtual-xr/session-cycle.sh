#!/usr/bin/env bash
# M3.5 repeated-session lifecycle test. One Monado service, five client exits.
set -uo pipefail
. "$(cd "$(dirname "$0")" && pwd)/common.sh"
SCRIPT="$TOOL_DIR/launch-virtual-xr.sh"
TRACE_SHIM="$SHIM"
FIFO="$BASE/session-cycle.stdin.fifo"
SUM="$BASE/session-cycle-summary.txt"
PID=""
FD_OPEN=0

cleanup() {
  if [ -n "${PID:-}" ] && kill -0 "$PID" 2>/dev/null; then
    kill -TERM "$PID" 2>/dev/null || true
    sleep 0.3
    kill -KILL "$PID" 2>/dev/null || true
  fi
  if [ "${FD_OPEN:-0}" = 1 ]; then exec 8>&- 2>/dev/null || true; fi
  rm -f "$FIFO" 2>/dev/null || true
  # Never stop the persistent service in this cleanup; stop is explicit.
}
trap cleanup EXIT INT TERM

rm -f "$SUM" "$FIFO"
: > "$SUM"
echo "service_start=$(date +%s.%N)" | tee -a "$SUM"
"$SCRIPT" start || exit 1
SERVICE_PID=$(cat "$PIDFILE" 2>/dev/null || true)
echo "service_pid=$SERVICE_PID" | tee -a "$SUM"

for cycle in 1 2 3 4 5; do
  TRACE="$BASE/session-cycle-${cycle}.trace.log"
  XRL="$BASE/session-cycle-${cycle}.hello-xr.log"
  rm -f "$TRACE" "$XRL"
  WINDOW_BEFORE=$(wmctrl -l 2>/dev/null | awk '/Monado!/ {print $1; exit}')
  SERVICE_ALIVE_BEFORE=$(kill -0 "$SERVICE_PID" 2>/dev/null && echo yes || echo no)
  echo "=== cycle $cycle start $(date +%s.%N) window_before=${WINDOW_BEFORE:-none} service_alive_before=$SERVICE_ALIVE_BEFORE ===" | tee -a "$SUM"

  rm -f "$FIFO"
  mkfifo "$FIFO" || { echo "cycle=$cycle result=fifo_failed" | tee -a "$SUM"; break; }
  exec 8<>"$FIFO" || { echo "cycle=$cycle result=fifo_open_failed" | tee -a "$SUM"; break; }
  FD_OPEN=1

  START=$(date +%s.%N)
  env XDG_RUNTIME_DIR="$RUNTIME_DIR" DISPLAY="$DISPLAY" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
    XR_RUNTIME_JSON="$RUNTIME_JSON" \
    LD_PRELOAD="$TRACE_SHIM" XR_INPUT_TRACE_LOG="$TRACE" \
    timeout --signal=TERM --kill-after=5s 12 "$HELLO_XR" -G OpenGL <&8 >"$XRL" 2>&1 &
  PID=$!
  READY=0
  for _ in $(seq 1 80); do
    sleep 0.1
    if rg -q 'SESSION_STATE state=5' "$TRACE" 2>/dev/null; then READY=1; break; fi
    if ! kill -0 "$PID" 2>/dev/null; then break; fi
  done
  FIRST_FRAME=""
  for _ in $(seq 1 20); do
    FIRST_FRAME=$(rg -m1 'HEAD_POSE' "$TRACE" 2>/dev/null || true)
    [ -n "$FIRST_FRAME" ] && break
    sleep 0.05
  done
  echo "cycle=$cycle focused=$READY session_pid=$PID" | tee -a "$SUM"
  if [ -n "$FIRST_FRAME" ]; then echo "cycle=$cycle first_pose=$FIRST_FRAME" | tee -a "$SUM"; fi
  # Allow a few frames, then send a newline to the FIFO stdin so hello_xr exits normally.
  sleep 0.5
  printf '\n' >&8 2>/dev/null || true
  EXITED=0
  for _ in $(seq 1 40); do
    if ! kill -0 "$PID" 2>/dev/null; then EXITED=1; break; fi
    sleep 0.1
  done
  if [ "$EXITED" -eq 0 ]; then
    echo "cycle=$cycle normal_exit=no forcing_term" | tee -a "$SUM"
    kill -TERM "$PID" 2>/dev/null || true
  else
    echo "cycle=$cycle normal_exit=yes" | tee -a "$SUM"
  fi
  set +e
  wait "$PID" 2>/dev/null
  RC=$?
  set -e
  END=$(date +%s.%N)
  SERVICE_ALIVE_AFTER=$(kill -0 "$SERVICE_PID" 2>/dev/null && echo yes || echo no)
  WINDOW_AFTER=$(wmctrl -l 2>/dev/null | awk '/Monado!/ {print $1; exit}')
  TRACE_STATES=$(rg 'SESSION_STATE' "$TRACE" 2>/dev/null | sed 's/.*state=//; s/ time=.*//' | tr '\n' ',' || true)
  HEAD_COUNT=$(rg -c 'HEAD_POSE' "$TRACE" 2>/dev/null || true)
    echo "cycle=$cycle exit_rc=$RC elapsed_s=$(awk "BEGIN{printf \"%.3f\", $END-$START}") service_alive_after=$SERVICE_ALIVE_AFTER window_after=${WINDOW_AFTER:-none} trace_states=${TRACE_STATES:-none} head_samples=${HEAD_COUNT:-0}" | tee -a "$SUM"
  exec 8>&- 2>/dev/null || true
  FD_OPEN=0
  rm -f "$FIFO"
  # Confirm the persistent service survives the idle gap as well as the client exit.
  sleep 1
  SERVICE_ALIVE_IDLE=$(kill -0 "$SERVICE_PID" 2>/dev/null && echo yes || echo no)
  echo "cycle=$cycle service_alive_after_1s=$SERVICE_ALIVE_IDLE" | tee -a "$SUM"
  if [ "$SERVICE_ALIVE_IDLE" != yes ]; then
    echo "cycle=$cycle result=service_died_after_client" | tee -a "$SUM"
    break
  fi
 done

echo "=== cycle test finished $(date +%s.%N) ===" | tee -a "$SUM"
if [ "${M35_STOP_AFTER_TEST:-0}" = 1 ]; then
  "$SCRIPT" stop >/dev/null 2>&1 || true
else
  echo "service_left_running=yes pid=$SERVICE_PID" | tee -a "$SUM"
fi
