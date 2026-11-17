#!/usr/bin/env bash
# Shared defaults for the virtual XR test harness.
#
# TOOL_DIR is this checkout's tools/testing/virtual-xr directory. BASE is the
# writable data/output directory used by the launcher. Keeping them separate
# means the scripts can live in a read-only checkout while the runtime socket,
# PID file and test output live under the user's data directory.
TOOL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE="${M35_BASE:-${VENTURE_VIRTUAL_BASE:-${XDG_DATA_HOME:-$HOME/.local/share}/venture-virtual-hmd}}"
RUNTIME_DIR="${VENTURE_VIRTUAL_RUNTIME_DIR:-$BASE/runtime}"
RUNTIME_JSON="${VENTURE_MONADO_JSON:-/usr/share/openxr/1/openxr_monado.json}"
DISPLAY="${DISPLAY:-:0}"
WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
HELLO_XR="${HELLO_XR:-hello_xr}"
SHIM="${VENTURE_XR_TRACE_SHIM:-$TOOL_DIR/libxrinputtrace.so}"
PIDFILE="$BASE/monado-service.pid"

export DISPLAY
export WAYLAND_DISPLAY
export XR_RUNTIME_JSON="$RUNTIME_JSON"
