<div align="center">

<img src="docs/media/logo.svg" width="72" alt="Venture logo">

# Venture

**A Linux-native Android and Quest compatibility runtime for PC VR.**

</div>

Venture runs Android and Quest-oriented applications on a Linux PC through its
Android compatibility layer and OpenXR integration. The project is a
compatibility runtime, not a general-purpose Android emulator: it adapts the
framework, native APIs, graphics and input paths that VR applications actually
use.

This repository is a development and hardware-testing branch of Venture. It is
not a release channel and does not claim general application compatibility. The
current work is deliberately narrow: make a small set of Android/Quest
applications start, render and connect reliably through real and virtual
OpenXR runtimes.

## Current development focus

- **Android framework and application compatibility.** Framework classes,
  lifecycle paths and Java APIs needed by Quest applications, including targeted
  fixes for observed startup and UI failures.
- **Android runtime and native API compatibility.** JIT execution, libc, JNI,
  linking, native libraries and the Android service surface.
- **OpenXR integration.** Quest applications through a host OpenXR runtime,
  including action, controller, layer and swapchain behavior.
- **Virtual headset and controller testing through Monado.** A portable,
  persistent QWERTY virtual XR environment for repeatable OpenXR and input
  testing.
- **Quest 3S and WiVRn hardware testing.** Physical-headset validation through
  WiVRn and the system OpenXR runtime on a Linux PC.
- **Graphics and rendering compatibility.** Vulkan/OpenGL ES translation, eye
  projections, swapchains, texture lifetime and related rendering fixes.
- **Android networking and certificate trust.** Guest networking behavior,
  including the Android system CA store used by native TLS clients.
- **EOS connectivity and multiplayer compatibility.** Authentication, messaging
  and lobby/room behavior in applications that use Epic Online Services.
- **Application startup and stability.** Conversion, class loading, startup
  diagnostics and crash/exit handling.

Only areas exercised by the current source or test history are listed here.
Compatibility is application-specific; a successful launch or XR session for one
title is not a promise for another.

## Multiplayer

The current multiplayer investigation is focused on the Android networking and
EOS path used by Quest applications.

### Root cause of the EOS TLS failure

In the tested application, EOS authentication succeeded but the messaging
connection failed and lobby hosting could not complete. The guest's native EOS
TLS client rejected Epic's valid server certificate with a fatal TLS alert:

```text
15 03 03 00 02 02 30
```

That is alert 48, `bad_certificate`. The host's own TLS client accepted
`api.epicgames.dev` with a valid chain, so the problem was not the network or the
server certificate. The guest Android environment had no system certificate
store at:

```text
/system/etc/security/cacerts
```

Java networking had its own trust path and could work independently, which made
the failure look inconsistent. EOS's native messaging client needed the Android
native CA store.

### The fix

Venture now populates the guest Android CA store automatically from the host's
hashed trusted root certificates. On first access to
`/system/etc/security/cacerts`, the runtime copies the `<subject-hash>.<n>` roots
from the host's certificate directories into the application's guest `/system`
volume. Existing guest roots are preserved, and broken or unreadable entries are
skipped individually.

The change is a trust-store bridge, not a TLS workaround:

- certificate verification remains enabled;
- only roots trusted by the host are made available;
- no Epic-specific certificate is hardcoded;
- no certificate pinning is bypassed;
- no private key, token or account credential is installed or logged;
- the guest store is recreated automatically and does not depend on a manually
  prepared data directory.

### Verified

- EOS authentication succeeds in the tested application.
- The EOS STOMP WebSocket messaging connection succeeds.
- The guest Android CA store is populated automatically from host trusted roots.
- 236 trusted roots were imported from the host's `/etc/ssl/certs` during the
  physical test.
- No TLS verification bypass is required.
- No `bad_certificate` alert occurred during the verified run.
- A public `SuperCity/FreeSwing` room was successfully hosted.
- The `SuperCity` scene loaded.
- The game ran at approximately 72 FPS during the physical-headset test.

### Not yet verified

- Successfully joining another player's confirmed-live room.
- Reliable EOS P2P connectivity between multiple players.
- General multiplayer compatibility across all supported applications.
- Voice chat and all EOS multiplayer services.

STOMP connectivity, authentication and room hosting are meaningful milestones,
but they do not prove that every multiplayer feature works. In the tested run,
the advertised rooms observed were unavailable or stale, so the game fell back to
hosting. Joining a confirmed-live peer still needs a dedicated test.

## Virtual XR testing

The repository includes a portable Monado-based test harness in
[`tools/testing/virtual-xr`](tools/testing/virtual-xr/README.md). It provides:

- a virtual HMD;
- virtual left and right QWERTY controllers;
- head-pose testing;
- independent left/right controller-pose testing;
- controller button and OpenXR action transitions;
- a persistent `monado-service` reused across application sessions;
- repeated OpenXR session and client-exit testing.

The harness starts one isolated `monado-service` with a private
`XDG_RUNTIME_DIR` and an explicit `XR_RUNTIME_JSON`. It does not edit the
system-wide OpenXR registration and does not start or stop WiVRn.

### Requirements

- Linux with an X11 or Xwayland desktop.
- Monado 25.1.0 or a compatible build with the QWERTY driver and debug GUI.
- An OpenXR runtime manifest, normally
  `/usr/share/openxr/1/openxr_monado.json`.
- `monado-service`, `hello_xr`, `wmctrl`, `xdotool`, `rg` and `ss`.
- A Venture AppImage for launching an application under the harness.
- A C compiler, `pkg-config` and OpenXR development headers for the optional
  input-trace shim.

### Setup and commands

From the repository root:

```bash
cd tools/testing/virtual-xr

# Optional: build the trace shim used by the input and session tests.
cc -shared -fPIC -o libxrinputtrace.so xr-input-trace.c \
  $(pkg-config --cflags --libs openxr) -ldl -lm

export VENTURE_APPIMAGE=/path/to/Venture.AppImage
# Optional, for a build whose integrity must be checked:
# export VENTURE_APPIMAGE_SHA256=<expected sha256>
```

The persistent launcher:

```bash
./launch-virtual-xr.sh start
./launch-virtual-xr.sh status
./launch-virtual-xr.sh game <package>
./launch-virtual-xr.sh stop
```

The lower-level tests run from the checkout and share the same data directory:

```bash
./session-cycle.sh
./controller-input-test.sh
./menu-button-test.sh
./perf-test.sh
```

`VENTURE_VIRTUAL_BASE` selects the writable runtime/output directory; it defaults
under `${XDG_DATA_HOME:-$HOME/.local/share}/venture-virtual-hmd`. A bounded game
run can use `VENTURE_GAME_TIMEOUT=SECONDS`. See the
[harness README](tools/testing/virtual-xr/README.md) for all environment
overrides and the service-lifecycle details.

The service is started with `XRT_NO_STDIN=1`,
`IPC_EXIT_ON_DISCONNECT=0` and `IPC_EXIT_WHEN_IDLE=0`, so it survives a client
exit or idle gap and remains available for the next session.

### Verification record

- [M3.5 virtual XR report](tools/testing/virtual-xr/M3.5-REPORT.md): five
  consecutive OpenGL `hello_xr` sessions on one service, head and controller
  pose checks, grip/action transitions, and sequential application sessions.
- [M3.6 AppsFlyer report](tools/testing/virtual-xr/M3.6-REPORT.md): the Roblox
  startup blocker caused by an absent proprietary AppsFlyer optional module.

The harness verifies the host OpenXR/input path. It does not replace physical
headset testing for cameras, microphone, passthrough, eye/hand tracking,
attestation or in-game controller behavior.

## Hardware testing

Physical testing uses:

- a Meta Quest 3S headset;
- WiVRn for the PC-to-headset stream;
- the system OpenXR runtime selected by WiVRn;
- a Linux PC running Venture.

This is separate from the virtual Monado harness. The virtual harness owns a
private runtime socket and never becomes the physical headset's runtime. The
hardware path uses the normal system OpenXR registration and the headset's real
display and input devices.

The verified EOS networking run used this physical path: the runtime, EOS
authentication, STOMP messaging, public room hosting, the `SuperCity` scene and
approximately 72 FPS were observed with the headset connected through WiVRn. That
is one application and one session, not a general hardware-compatibility claim.

## Build and development

The verified development path is Linux x86_64. The project uses C++23, CMake
3.28 or newer, Ninja, Python 3, and a JDK provisioned through the repository's
pinned toolchain helper. The pinned CMake, Ninja, JDK, ANGLE and FFmpeg
dependencies are kept under `.deps/` and are not part of the repository.

Provision the Linux toolchain from the repository root:

```bash
python3 tools/bootstrap.py --jdk
```

Configure and build:

```bash
.deps/cmake/bin/cmake --preset linux-release
.deps/cmake/bin/cmake --build --preset linux-release \
  --target venture-runtime venture_tests venture_xr_tests -j"$(nproc)"
```

The runtime is produced at `build/linux-release/bin/venture-runtime`. The
focused test binaries are produced in the same output directory.

Focused regression commands:

```bash
build/linux-release/bin/venture_tests \
  --test-case="libc B: /system/etc/security/cacerts*"

build/linux-release/bin/venture_xr_tests \
  --test-case="*Aggregate vibration*"
```

The guest CA-store test checks hashed-root copying, preservation of existing
guest roots, rejection of non-root files, and survival of dangling or non-file
entries. The XR test checks aggregate-action fallback when the host rejects
native subaction roots.

## Current limitations

- Joining a confirmed-live player's room and reliable player-to-player EOS P2P
  connectivity are not yet verified.
- Multiplayer support is application- and service-specific. Authentication,
  messaging and hosting in the tested application do not establish general
  multiplayer compatibility.
- Voice chat and other EOS services are untested.
- The physical-hardware result covers one EOS application and session. It is not
  a compatibility guarantee for other titles or other PC/headset combinations.
- The virtual Monado harness covers OpenXR and input plumbing, not in-game
  behavior, visual quality, cameras, microphone, passthrough, eye/hand tracking
  or attestation.
- Roblox startup remains blocked before XR by the absent proprietary AppsFlyer
  `exception-manager` module. Venture does not ship or fabricate that module; see
  the [M3.6 report](tools/testing/virtual-xr/M3.6-REPORT.md).
- Broader Android application and game compatibility is an active investigation,
  not a current guarantee.
- The complete `linux-release` target set currently stops in an unrelated
  existing test source, `tests/runtime/output_mirror_test.cpp`, because it uses
  `std::ref` without including `<functional>`. The runtime and focused test
  targets above build and run.
- If the host has no usable trusted-root directory, the guest CA store cannot
  validate public certificates; the runtime logs the missing host store rather
  than silently disabling verification.

The current branch preserves the earlier M2.1 framework/audio compatibility work,
M3.3 WindowInsets changes, M3.5 virtual XR harness, and M3.6 AppsFlyer diagnosis
in its history. Those changes are not a claim that every application using those
APIs now works.

## Project status

**Implemented**

- Android framework and native API compatibility work.
- OpenXR integration and Quest input/action handling.
- Automatic guest Android CA-store population from host trusted roots.
- A reusable Monado QWERTY virtual XR harness.
- Focused regression tests for the CA store and OpenXR aggregate actions.

**Tested**

- Guest CA-store population from the host's hashed roots.
- EOS authentication and STOMP connectivity in the tested application.
- Hosting a public `SuperCity/FreeSwing` room.
- Loading the `SuperCity` scene.
- Five consecutive virtual `hello_xr` sessions on one persistent Monado service.
- Virtual head, controller-pose and grip/action transitions.

**Working on physical hardware**

- The tested EOS application on a Quest 3S through WiVRn.
- Rendering and frame submission in that session, at approximately 72 FPS.

**Working only in the virtual XR environment**

- The portable Monado QWERTY harness and its session/input scripts.
- Repeatable OpenXR session cycling and controller-pose tests.

**Not yet verified**

- Joining a confirmed-live player's room.
- Multi-player EOS P2P transport.
- Voice and the full EOS service surface.
- General multiplayer compatibility.
- General compatibility across the wider Quest/Android application library.

## License and notices

Venture is licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). Third-party
licenses are recorded in [LICENSES/THIRD_PARTY_NOTICES.md](LICENSES/THIRD_PARTY_NOTICES.md).
Venture is not affiliated with Meta; see [DISCLAIMER.md](DISCLAIMER.md).
