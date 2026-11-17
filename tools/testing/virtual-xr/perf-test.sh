#!/usr/bin/env bash
# M3.5 performance/resource sample for the isolated Monado QWERTY service.
# Uses one normal OpenGL hello_xr session on the already-running service.
set -uo pipefail
. "$(cd "$(dirname "$0")" && pwd)/common.sh"
SCRIPT="$TOOL_DIR/launch-virtual-xr.sh"
TRACE="$BASE/perf-test.trace.log"
XRL="$BASE/perf-test.hello-xr.log"
SUM="$BASE/perf-test-summary.txt"
FIFO="$BASE/perf-test.stdin.fifo"
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
}
trap cleanup EXIT INT TERM
record() { printf '%s\n' "$*" | tee -a "$SUM"; }

rm -f "$TRACE" "$XRL" "$SUM" "$FIFO"
: > "$SUM"
"$SCRIPT" start >/dev/null 2>&1 || { echo "RESULT=FAIL reason=start" | tee -a "$SUM"; exit 1; }
SERVICE_PID=$(cat "$PIDFILE")
record "service_pid=$SERVICE_PID"
record "service_runtime=$RUNTIME_DIR"

mkfifo "$FIFO" || { record "RESULT=FAIL reason=fifo"; exit 1; }
exec 8<>"$FIFO" || { record "RESULT=FAIL reason=fifo_open"; exit 1; }
FD_OPEN=1
START_WALL=$(date +%s.%N)
env XDG_RUNTIME_DIR="$RUNTIME_DIR" DISPLAY="$DISPLAY" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
  XR_RUNTIME_JSON="$RUNTIME_JSON" \
  LD_PRELOAD="$SHIM" XR_INPUT_TRACE_LOG="$TRACE" \
  timeout --signal=TERM --kill-after=5s 30 "$HELLO_XR" -G OpenGL <&8 >"$XRL" 2>&1 &
PID=$!
READY=0
for _ in $(seq 1 120); do
  sleep 0.1
  if rg -q 'SESSION_STATE state=5' "$TRACE" 2>/dev/null &&
     rg -q 'FRAME n=' "$TRACE" 2>/dev/null; then READY=1; break; fi
  if ! kill -0 "$PID" 2>/dev/null; then break; fi
done
FOCUSED_WALL=$(date +%s.%N)
CLIENT_PID=$(pgrep -x hello_xr | head -1 || true)
record "focused=$READY client_pid=${CLIENT_PID:-unknown}"
if [ "$READY" -ne 1 ]; then
  record "RESULT=FAIL reason=not_focused_or_no_frames"
  tail -20 "$XRL" | tee -a "$SUM"
  exit 1
fi

# Let the steady-state frame loop run for a little over one second.
sleep 1.2
SAMPLE_PS=$(ps -o pid=,comm=,%cpu=,%mem=,rss= -p "$SERVICE_PID" ${CLIENT_PID:+$CLIENT_PID} 2>/dev/null || true)
record "resource_sample_ps:"
printf '%s\n' "$SAMPLE_PS" | sed 's/^/  /' | tee -a "$SUM"

# Give the loop another second, then exit normally through hello_xr's FIFO.
sleep 0.8
STOP_START=$(date +%s.%N)
printf '\n' >&8 2>/dev/null || true
for _ in $(seq 1 50); do
  kill -0 "$PID" 2>/dev/null || break
  sleep 0.1
done
NORMAL_EXIT=yes
if kill -0 "$PID" 2>/dev/null; then
  NORMAL_EXIT=no
  kill -TERM "$PID" 2>/dev/null || true
fi
set +e
wait "$PID" 2>/dev/null
RC=$?
set -e
STOP_END=$(date +%s.%N)
exec 8>&- 2>/dev/null || true
FD_OPEN=0
rm -f "$FIFO"

TRACE_FIRST=$(head -1 "$TRACE" | awk '{print $1}')
STATE5_TS=$(rg 'SESSION_STATE state=5' "$TRACE" | head -1 | awk '{print $1}')
FIRST_FRAME_TS=$(rg 'FRAME n=1 ' "$TRACE" | head -1 | awk '{print $1}')
FRAME_STATS=$(awk '
/FRAME n=/ {
  ts=$1; n++;
  if (first=="") first=ts;
  if (prev!="") {
    d=ts-prev; sum+=d;
    if (!have || d<min) {min=d; have=1;}
    if (d>max) max=d;
  }
  last=ts;
  prev=ts;
}
END {
  printf "frames=%d first=%.6f last=%.6f avg_dt_ms=%.3f min_dt_ms=%.3f max_dt_ms=%.3f avg_fps=%.2f\n",
    n, first, last, (n>1?sum/(n-1)*1000:0), min*1000, max*1000,
    (last>first?(n-1)/(last-first):0);
}' "$TRACE")
FRAME_ERRORS=$(rg -c 'FRAME_ERROR' "$TRACE" 2>/dev/null || true)
STATES=$(rg 'SESSION_STATE' "$TRACE" | sed 's/.*state=//; s/ time=.*//' | tr '\n' ',' || true)
FOCUS_DELTA=$(awk -v a="$TRACE_FIRST" -v b="$STATE5_TS" 'BEGIN{printf "%.3f", b-a}')
FRAME_DELTA=$(awk -v a="$TRACE_FIRST" -v b="$FIRST_FRAME_TS" 'BEGIN{printf "%.3f", b-a}')
record "session_focused_trace_delta_s=$FOCUS_DELTA"
record "first_frame_after_trace_start_s=$FRAME_DELTA"
record "first_frame_vs_focused_signed_s=$(awk -v a="$STATE5_TS" -v b="$FIRST_FRAME_TS" 'BEGIN{printf "%.3f", b-a}')"
record "wall_focused_after_process_start_s=$(awk -v a="$START_WALL" -v b="$FOCUSED_WALL" 'BEGIN{printf "%.3f", b-a}')"
record "wall_shutdown_s=$(awk -v a="$STOP_START" -v b="$STOP_END" 'BEGIN{printf "%.3f", b-a}')"
record "normal_exit=$NORMAL_EXIT client_exit_rc=$RC"
record "frame_stats=$FRAME_STATS"
record "frame_errors=${FRAME_ERRORS:-0}"
record "session_states=$STATES"
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
  record "service_still_running=yes"
else
  record "service_still_running=no"
fi
if [ "$READY" -eq 1 ] && [ "$NORMAL_EXIT" = yes ] && [ "$RC" -eq 0 ] && [ "${FRAME_ERRORS:-0}" -eq 0 ] && [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
  record "RESULT=PASS"
  exit 0
else
  record "RESULT=FAIL"
  exit 1
fi
