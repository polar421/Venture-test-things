# Virtual XR test harness

This directory contains the portable M3.5 tooling for testing Venture against a
persistent, isolated Monado QWERTY virtual headset. It is a development harness,
not a physical-headset compatibility result.

The harness starts one `monado-service` with a private `XDG_RUNTIME_DIR` and an
explicit `XR_RUNTIME_JSON`, so the system-wide OpenXR registration and WiVRn are
left alone. It does not require a physical Quest.

## Requirements

- Linux with an X11 or Xwayland desktop.
- Monado 25.1.0 or a compatible build with the QWERTY driver and debug GUI.
- An OpenXR runtime manifest, normally
  `/usr/share/openxr/1/openxr_monado.json`.
- `monado-service`, `hello_xr`, `wmctrl`, `xdotool`, `rg`, and `ss`.
- A Venture AppImage. The M3.3 Roblox window-insets candidate is known to work
  with the harness; use its exact checksum when testing that build.
- A C compiler and OpenXR development headers to build the optional trace shim.

The harness assumes the normal `DISPLAY`/`WAYLAND_DISPLAY` for the desktop. Set
them explicitly when running from a non-graphical shell.

## Setup

From the repository root:

```bash
cd tools/testing/virtual-xr

# Optional, for pose/action tracing and the input tests.
cc -shared -fPIC -o libxrinputtrace.so xr-input-trace.c \
  $(pkg-config --cflags --libs openxr) -ldl -lm

export VENTURE_APPIMAGE="$HOME/AppImages/Venture-0.2.4.1-M3.3-window-insets-20261116-x86_64.AppImage"
export VENTURE_APPIMAGE_SHA256="9a9aec8e63260986382366e357a2047c59025c06188be51867015d7be00e7217"
export VENTURE_VIRTUAL_BASE="$HOME/.local/share/venture-virtual-hmd"
```

`VENTURE_APPIMAGE` is required by `game`; `start` and the OpenXR tests do not
need it. If `VENTURE_APPIMAGE_SHA256` is omitted, the launcher prints the actual
hash and continues. Set it for any candidate whose integrity matters.

The default data directory is
`${XDG_DATA_HOME:-$HOME/.local/share}/venture-virtual-hmd`. It holds the private
runtime directory, service log, PID file and test output. These generated files
are ignored by Git.

## Commands

```bash
./launch-virtual-xr.sh start
./launch-virtual-xr.sh status
./launch-virtual-xr.sh test          # five hello_xr sessions on one service
./launch-virtual-xr.sh input-test    # head + both QWERTY controllers
./launch-virtual-xr.sh game com.roblox.client
./launch-virtual-xr.sh env           # exports for a manual launch
./launch-virtual-xr.sh stop
```

`start` is idempotent and refuses to compete with an unknown listener on the
private socket. `stop` only stops the service PID recorded by the harness and
checks that the process is `monado-service` before signalling it.

Set `VENTURE_GAME_TIMEOUT=SECONDS` to bound a game run:

```bash
VENTURE_GAME_TIMEOUT=60 ./launch-virtual-xr.sh game com.roblox.client
```

The lower-level scripts are also runnable directly:

```bash
./session-cycle.sh
./controller-input-test.sh
./menu-button-test.sh
./perf-test.sh
```

They use the same `VENTURE_VIRTUAL_BASE`, `VENTURE_VIRTUAL_RUNTIME_DIR`,
`VENTURE_MONADO_JSON`, `HELLO_XR`, `DISPLAY`, and `WAYLAND_DISPLAY` overrides as
the launcher. `M35_BASE` is accepted as a backwards-compatible alias for the
data directory. Output goes under that directory and is intentionally not
tracked.

## Runtime selection

For every client the harness exports:

```bash
XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json
XDG_RUNTIME_DIR=$VENTURE_VIRTUAL_BASE/runtime
```

The private directory contains the IPC socket (`monado_comp_ipc`) and the PID
file. `active_runtime.json` is never edited, WiVRn is never started or stopped,
and no second Monado service is started while the harness owns one.

## Why `XRT_NO_STDIN=1`

Standalone `monado-service` polls stdin. Its main loop treats stdin readiness or
HUP as a shutdown request. A launcher that leaves stdin connected to a terminal,
pipe, or exhausted FIFO can therefore make the service exit after a client
disconnects:

```text
[client_disconnected] Client 1 disconnected
[ipc_server_main_common] Server exiting: '0'
```

That takes the IPC socket and the one-time QWERTY/debug GUI with it, so the
next client cannot reach `FOCUSED`.

The launcher starts the service with:

```bash
XRT_NO_STDIN=1
IPC_EXIT_ON_DISCONNECT=0
IPC_EXIT_WHEN_IDLE=0
```

and redirects stdin from `/dev/null`. `XRT_NO_STDIN=1` is the essential setting;
the other two prevent a client disconnect or idle gap from stopping the service.
The service is then reused across clients and explicitly stopped only by
`launch-virtual-xr.sh stop`.

## M3.5 verification summary

The sanitized report in [M3.5-REPORT.md](M3.5-REPORT.md) records:

- one persistent service reused for five consecutive OpenGL `hello_xr` sessions;
- every session reached `FOCUSED` and exited normally;
- measured head translation and rotation in both directions;
- independent left and right controller pose changes;
- real `grab_object` press/release transitions and a real menu-button
  `quit_session` transition;
- sequential Super RC, Untangled, and BattleGlide XR sessions;
- Roblox remaining blocked before XR by its AppsFlyer nested-class error.

The evidence is limited to the virtual Monado/QWERTY path. No physical Quest,
WiVRn, microphone, camera, passthrough, attestation, or in-game input result is
claimed by this harness.

## Files

- `launch-virtual-xr.sh` — service lifecycle and game launcher.
- `session-cycle.sh` — five-session persistence test.
- `controller-input-test.sh` — head and both-controller OpenXR checks.
- `menu-button-test.sh` — focused menu-button diagnostics.
- `perf-test.sh` — generic process/frame timing sample.
- `xr-input-trace.c` — OpenXR trace shim source.
- `common.sh` — shared paths and environment defaults.
- `M3.5-REPORT.md` — sanitized results and limitations.

Raw traces, logs, PIDs, compiled shims, and summaries are local artifacts and are
not intended for commit.
