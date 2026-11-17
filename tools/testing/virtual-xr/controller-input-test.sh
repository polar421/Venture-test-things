#!/usr/bin/env bash
# M3.5 QWERTY input verification against one persistent isolated Monado service.
# Sends real X11 key/mouse events to the Monado SDL window and records actual
# OpenXR view/action/space states through the LD_PRELOAD trace shim.
set -uo pipefail
. "$(cd "$(dirname "$0")" && pwd)/common.sh"
SCRIPT="$TOOL_DIR/launch-virtual-xr.sh"
TRACE="$BASE/controller-input.trace.log"
XRL="$BASE/controller-input.hello-xr.log"
SUM="$BASE/controller-input-summary.txt"
FIFO="$BASE/controller-input.stdin.fifo"
MONADO_ID=""
PID=""
FD_OPEN=0
FAIL=0

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
fail() { record "RESULT=FAIL reason=$*"; FAIL=1; }
latest_line() { rg "$1" "$TRACE" 2>/dev/null | tail -1; }
pose_pos() { sed -n 's/.*pos=(\([^)]*\)).*/\1/p' <<<"$1"; }
pose_quat() { sed -n 's/.*quat=(\([^)]*\)).*/\1/p' <<<"$1"; }
num_delta() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.6f", (a>b?a-b:b-a)}'; }
signed_delta() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.6f", b-a}'; }
coord() { awk -F, -v n="$2" '{gsub(/[() ]/,"",$n); print $n}' <<<"$1"; }

rm -f "$TRACE" "$XRL" "$SUM" "$FIFO"
: > "$SUM"
"$SCRIPT" start || { echo "RESULT=FAIL reason=service_start"; exit 1; }
"$SCRIPT" status | tee -a "$SUM"
if [ -f "$PIDFILE" ]; then
  SERVICE_PID=$(cat "$PIDFILE")
  record "service_pid=$SERVICE_PID"
fi
for _ in $(seq 1 60); do
  MONADO_ID=$(wmctrl -l 2>/dev/null | awk '/Monado!/ {print $1; exit}')
  [ -n "$MONADO_ID" ] && break
  sleep 0.1
done
[ -n "$MONADO_ID" ] || { fail no_monado_window; exit 1; }
record "window=$MONADO_ID"

mkfifo "$FIFO" || { fail fifo_create; exit 1; }
exec 8<>"$FIFO" || { fail fifo_open; exit 1; }
FD_OPEN=1
env XDG_RUNTIME_DIR="$RUNTIME_DIR" DISPLAY="$DISPLAY" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
  XR_RUNTIME_JSON="$RUNTIME_JSON" \
  LD_PRELOAD="$SHIM" XR_INPUT_TRACE_LOG="$TRACE" \
  timeout --signal=TERM --kill-after=5s 45 "$HELLO_XR" -G OpenGL <&8 >"$XRL" 2>&1 &
PID=$!
ready=0
for _ in $(seq 1 120); do
  sleep 0.1
  if rg -q 'SESSION_STATE state=5' "$TRACE" 2>/dev/null &&
     rg -q 'CONTROLLER_POSE name=hand_pose sub=1' "$TRACE" 2>/dev/null &&
     rg -q 'CONTROLLER_POSE name=hand_pose sub=2' "$TRACE" 2>/dev/null &&
     rg -q 'FRAME n=' "$TRACE" 2>/dev/null; then ready=1; break; fi
  if ! kill -0 "$PID" 2>/dev/null; then break; fi
done
record "focused_and_controllers=$ready client_pid=$PID"
if [ "$ready" -ne 1 ]; then
  fail no_focused_controller_frames
  tail -20 "$XRL" | tee -a "$SUM"
  exit 1
fi
sleep 0.4
cp "$TRACE" "$BASE/controller-input-baseline.trace.log" 2>/dev/null || true

# --- HMD movement: A translates, Right rotates. Window-targeted X events keep
# the QWERTY window from needing to steal desktop focus for keyboard tests.
BASE_HEAD=$(latest_line 'HEAD_POSE name=view sub=0')
BASE_HEAD_POS=$(pose_pos "$BASE_HEAD"); BASE_HEAD_QUAT=$(pose_quat "$BASE_HEAD")
record "head_baseline pos=$BASE_HEAD_POS quat=$BASE_HEAD_QUAT"
xdotool keydown --window "$MONADO_ID" a >/dev/null 2>&1 || fail head_a_keydown
sleep 1.0
xdotool keyup --window "$MONADO_ID" a >/dev/null 2>&1 || fail head_a_keyup
sleep 0.5
A_HEAD=$(latest_line 'HEAD_POSE name=view sub=0')
A_HEAD_POS=$(pose_pos "$A_HEAD"); A_HEAD_QUAT=$(pose_quat "$A_HEAD")
record "head_after_A pos=$A_HEAD_POS quat=$A_HEAD_QUAT"
A_DX=$(num_delta "$(coord "$BASE_HEAD_POS" 1)" "$(coord "$A_HEAD_POS" 1)")
A_DY=$(num_delta "$(coord "$BASE_HEAD_POS" 2)" "$(coord "$A_HEAD_POS" 2)")
A_DZ=$(num_delta "$(coord "$BASE_HEAD_POS" 3)" "$(coord "$A_HEAD_POS" 3)")
record "head_A_position_delta dx=$A_DX dy=$A_DY dz=$A_DZ"
awk -v x="$A_DX" -v y="$A_DY" -v z="$A_DZ" 'BEGIN{exit !(x>0.01 || y>0.01 || z>0.01)}' || fail head_A_no_pose_change

# Opposite translation: D must move the HMD back in the opposite x direction.
xdotool keydown --window "$MONADO_ID" d >/dev/null 2>&1 || fail head_d_keydown
sleep 0.8
xdotool keyup --window "$MONADO_ID" d >/dev/null 2>&1 || fail head_d_keyup
sleep 0.4
D_HEAD=$(latest_line 'HEAD_POSE name=view sub=0')
D_HEAD_POS=$(pose_pos "$D_HEAD")
record "head_after_D pos=$D_HEAD_POS"
D_SIGNED_DX=$(signed_delta "$(coord "$A_HEAD_POS" 1)" "$(coord "$D_HEAD_POS" 1)")
record "head_A_to_D_opposite_dx=$D_SIGNED_DX"
awk -v d="$D_SIGNED_DX" 'BEGIN{exit !(d>0.01)}' || fail head_D_no_opposite_translation

xdotool keydown --window "$MONADO_ID" Right >/dev/null 2>&1 || fail head_right_keydown
sleep 0.8
xdotool keyup --window "$MONADO_ID" Right >/dev/null 2>&1 || fail head_right_keyup
sleep 0.5
R_HEAD=$(latest_line 'HEAD_POSE name=view sub=0')
R_HEAD_POS=$(pose_pos "$R_HEAD"); R_HEAD_QUAT=$(pose_quat "$R_HEAD")
record "head_after_Right pos=$R_HEAD_POS quat=$R_HEAD_QUAT"
R_QX=$(num_delta "$(coord "$A_HEAD_QUAT" 1)" "$(coord "$R_HEAD_QUAT" 1)")
R_QY=$(num_delta "$(coord "$A_HEAD_QUAT" 2)" "$(coord "$R_HEAD_QUAT" 2)")
R_QZ=$(num_delta "$(coord "$A_HEAD_QUAT" 3)" "$(coord "$R_HEAD_QUAT" 3)")
record "head_Right_orientation_delta dx=$R_QX dy=$R_QY dz=$R_QZ"
awk -v x="$R_QX" -v y="$R_QY" -v z="$R_QZ" 'BEGIN{exit !(x>0.01 || y>0.01 || z>0.01)}' || fail head_Right_no_orientation_change

# Opposite rotation: Left must rotate back toward the pre-Right orientation.
xdotool keydown --window "$MONADO_ID" Left >/dev/null 2>&1 || fail head_left_keydown
sleep 0.8
xdotool keyup --window "$MONADO_ID" Left >/dev/null 2>&1 || fail head_left_keyup
sleep 0.4
L_HEAD=$(latest_line 'HEAD_POSE name=view sub=0')
L_HEAD_QUAT=$(pose_quat "$L_HEAD")
record "head_after_Left pos=$(pose_pos "$L_HEAD") quat=$L_HEAD_QUAT"
L_QY=$(signed_delta "$(coord "$R_HEAD_QUAT" 2)" "$(coord "$L_HEAD_QUAT" 2)")
L_QY_ABS=$(num_delta "$(coord "$R_HEAD_QUAT" 2)" "$(coord "$L_HEAD_QUAT" 2)")
record "head_Right_to_Left_opposite_qy=$L_QY (magnitude=$L_QY_ABS)"
awk -v d="$L_QY_ABS" 'BEGIN{exit !(d>0.01)}' || fail head_Left_no_opposite_rotation

# Put the controllers back to their known local poses before controller tests.
xdotool key --window "$MONADO_ID" r >/dev/null 2>&1 || true
sleep 0.8
L0=$(latest_line 'CONTROLLER_POSE name=hand_pose sub=1')
R0=$(latest_line 'CONTROLLER_POSE name=hand_pose sub=2')
L0P=$(pose_pos "$L0"); R0P=$(pose_pos "$R0")
record "controller_baseline left=$L0P right=$R0P"

# --- Left controller: hold LCTRL and translate left with A. This is the QWERTY
# driver's documented focused-device path, not a synthetic OpenXR value.
xdotool keydown --window "$MONADO_ID" Control_L >/dev/null 2>&1 || fail left_ctrl_down
sleep 0.2
xdotool keydown --window "$MONADO_ID" a >/dev/null 2>&1 || fail left_a_down
sleep 1.0
xdotool keyup --window "$MONADO_ID" a >/dev/null 2>&1 || fail left_a_up
sleep 0.3
xdotool keyup --window "$MONADO_ID" Control_L >/dev/null 2>&1 || fail left_ctrl_up
sleep 0.6
L1=$(latest_line 'CONTROLLER_POSE name=hand_pose sub=1')
R1=$(latest_line 'CONTROLLER_POSE name=hand_pose sub=2')
L1P=$(pose_pos "$L1"); R1P=$(pose_pos "$R1")
record "after_left_controller_A left=$L1P right=$R1P"
L_DX=$(num_delta "$(coord "$L0P" 1)" "$(coord "$L1P" 1)")
R_DX=$(num_delta "$(coord "$R0P" 1)" "$(coord "$R1P" 1)")
record "left_controller_position_delta left_dx=$L_DX right_unchanged_dx=$R_DX"
awk -v d="$L_DX" 'BEGIN{exit !(d>0.01)}' || fail left_controller_no_pose_change

# Reset both controllers, then exercise the right controller independently.
xdotool key --window "$MONADO_ID" r >/dev/null 2>&1 || true
sleep 0.8
L2=$(latest_line 'CONTROLLER_POSE name=hand_pose sub=1')
R2=$(latest_line 'CONTROLLER_POSE name=hand_pose sub=2')
L2P=$(pose_pos "$L2"); R2P=$(pose_pos "$R2")
xdotool keydown --window "$MONADO_ID" Alt_L >/dev/null 2>&1 || fail right_alt_down
sleep 0.2
xdotool keydown --window "$MONADO_ID" d >/dev/null 2>&1 || fail right_d_down
sleep 1.0
xdotool keyup --window "$MONADO_ID" d >/dev/null 2>&1 || fail right_d_up
sleep 0.3
xdotool keyup --window "$MONADO_ID" Alt_L >/dev/null 2>&1 || fail right_alt_up
sleep 0.6
L3=$(latest_line 'CONTROLLER_POSE name=hand_pose sub=1')
R3=$(latest_line 'CONTROLLER_POSE name=hand_pose sub=2')
L3P=$(pose_pos "$L3"); R3P=$(pose_pos "$R3")
record "after_right_controller_D left=$L3P right=$R3P"
R3_DX=$(num_delta "$(coord "$R2P" 1)" "$(coord "$R3P" 1)")
L3_DX=$(num_delta "$(coord "$L2P" 1)" "$(coord "$L3P" 1)")
record "right_controller_position_delta right_dx=$R3_DX left_unchanged_dx=$L3_DX"
awk -v d="$R3_DX" 'BEGIN{exit !(d>0.01)}' || fail right_controller_no_pose_change

# --- Real mouse grip transitions, right (default FC) and left (LCTRL FC).
activate_qwerty() {
  wmctrl -ia "$MONADO_ID" >/dev/null 2>&1 || true
  xdotool windowactivate --sync "$MONADO_ID" >/dev/null 2>&1 || true
  xdotool mousemove --window "$MONADO_ID" 400 300 >/dev/null 2>&1 || true
  sleep 0.3
}
grip_phase() {
  local side=$1 prefix=$2
  local start end
  start=$(wc -l < "$TRACE")
  activate_qwerty
  if [ "$side" = left ]; then
    xdotool keydown --window "$MONADO_ID" Control_L >/dev/null 2>&1 || fail grip_left_ctrl_down
    sleep 0.2
  fi
  xdotool mousedown 2 >/dev/null 2>&1 || fail "${prefix}_mousedown"
  sleep 0.7
  xdotool mouseup 2 >/dev/null 2>&1 || fail "${prefix}_mouseup"
  sleep 0.5
  if [ "$side" = left ]; then
    xdotool keyup --window "$MONADO_ID" Control_L >/dev/null 2>&1 || fail grip_left_ctrl_up
    sleep 0.2
  fi
  end=$(wc -l < "$TRACE")
  local lines
  lines=$(tail -n +$((start + 1)) "$TRACE" | rg 'ACTION_FLOAT name=grab_object' || true)
  printf '%s\n' "$lines" > "$BASE/controller-${prefix}-grip-lines.txt"
  record "--- ${prefix}_grip action slice ---"
  printf '%s\n' "$lines" | tee -a "$SUM"
  if printf '%s\n' "$lines" | rg -q 'current=1\.000000'; then
    record "${prefix}_grip_press=PASS"
  else
    fail "${prefix}_grip_no_press_transition"
  fi
  if printf '%s\n' "$lines" | rg -q 'current=0\.000000'; then
    record "${prefix}_grip_release=PASS"
  else
    fail "${prefix}_grip_no_release_transition"
  fi
}
# Default FC is the right controller.
grip_phase right right
# LCTRL selects the left controller for both controller-specific click methods.
grip_phase left left

# --- Optional menu/system button evidence. hello_xr exposes quit_session; the
# QWERTY N key is a documented menu click. Do this last because it can end the
# session. Record the actual boolean transition if it occurs.
MENU_START=$(wc -l < "$TRACE")
activate_qwerty
# Hold the documented menu button long enough for an action sync; a bare
# xdotool key press/release can fall between two frames.
xdotool keydown --window "$MONADO_ID" n >/dev/null 2>&1 || true
sleep 0.8
xdotool keyup --window "$MONADO_ID" n >/dev/null 2>&1 || true
sleep 0.8
MENU_LINES=$(tail -n +$((MENU_START + 1)) "$TRACE" | rg 'ACTION_BOOL name=quit_session' || true)
printf '%s\n' "$MENU_LINES" > "$BASE/controller-menu-lines.txt"
record "--- menu_button action slice ---"
printf '%s\n' "$MENU_LINES" | tee -a "$SUM" || true
if printf '%s\n' "$MENU_LINES" | rg -q 'current=1'; then
  record "menu_button_press=PASS (actual quit_session boolean transition)"
else
  record "menu_button_press=UNVERIFIED (hello_xr did not expose a menu transition)"
fi

# --- Session/frame summary and normal shutdown.
FRAME_COUNT=$(rg -c 'FRAME n=' "$TRACE" 2>/dev/null || true)
FRAME_ERRORS=$(rg -c 'FRAME_ERROR' "$TRACE" 2>/dev/null || true)
record "frames_submitted=${FRAME_COUNT:-0} frame_errors=${FRAME_ERRORS:-0}"
record "session_states=$(rg 'SESSION_STATE' "$TRACE" | sed 's/.*state=//; s/ time=.*//' | tr '\n' ',' || true)"
printf '\n' >&8 2>/dev/null || true
for _ in $(seq 1 40); do
  kill -0 "$PID" 2>/dev/null || break
  sleep 0.1
done
if kill -0 "$PID" 2>/dev/null; then
  record "normal_exit=no"
  kill -TERM "$PID" 2>/dev/null || true
else
  record "normal_exit=yes"
fi
set +e; wait "$PID" 2>/dev/null; RC=$?; set -e
record "client_exit_rc=$RC"
exec 8>&- 2>/dev/null || true
FD_OPEN=0
rm -f "$FIFO"
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
  record "service_still_running=yes pid=$(cat "$PIDFILE")"
else
  record "service_still_running=no"
  FAIL=1
fi
if [ "$FAIL" -eq 0 ]; then record "RESULT=PASS"; else record "RESULT=FAIL"; fi
exit "$FAIL"
