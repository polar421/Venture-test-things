# Native Quest multiplayer interoperability

This document replaces the earlier plan of a Venture-hosted lobby as the way to
play together. A Venture-only session system cannot achieve the goal: a regular
Meta Quest player, running the unmodified game, has to be able to meet a Venture
player inside the game's own multiplayer. That is the target described here.

**Nothing in this document is claimed as working.** No Quest headset, game build
or Meta account exists in the environment this was written in. Every status below
is either a source-code fact (checkable in this repository) or explicitly marked
untested. Where a status says "implemented", it means the code path exists and
performs the described operation; it does **not** mean Meta accepted it or that a
game worked with it.

---

## 1. State of the fork

| Item | Finding |
|---|---|
| Fork HEAD | `66de673` |
| Upstream `Villagers654/Venture` `main` | `66de673` — **identical** |
| Fork-specific commits | none |
| Existing multiplayer implementation | none |
| Linux support | upstream's own (CMake presets, POSIX platform layer, `.deb`/`.rpm`/AppImage/Flatpak packaging) |
| Uncommitted work found | none (`git status` clean at start) |

So this is not a fork with half-finished multiplayer to preserve. It is upstream
Venture 0.2.3.6, whose Meta platform emulation (`src/runtime/hle/ovrplatform/`)
is the subsystem that decides whether multiplayer across the Quest ecosystem can
work at all.

## 2. What a Quest game's multiplayer is actually made of

Four independent layers. A game can use any subset, and *which* subset it uses
decides whether a Venture player can join a native Quest player.

1. **Game transport.** The title's own networking: raw UDP/TCP, ENet, Photon,
   PlayFab/Unity Gaming Services, Epic Online Services, a studio backend, or
   direct IP. Sessions are found by a room code, a server browser, the game's own
   friend list, or the studio's matchmaker.
2. **Player identity.** Meta account ID plus an **app-scoped** access token and a
   user-proof nonce, which the game's backend verifies with Meta before trusting
   the player.
3. **Platform session layer.** Meta Platform SDK calls: `ovr_GroupPresence_*`
   (presence and joinable lobby/match ids), `ovr_Party_*`, `ovr_Matchmaking_*`,
   `ovr_NetSync_*` (positional voice and state sync), and the system panels
   `LaunchInvitePanel`, `LaunchRosterPanel`, `LaunchRejoinDialog`.
4. **Meta push channel (FBNS) and OS deep links.** How an invitation or "Join"
   tap actually reaches a headset, and how the OS launches the game into the
   invited session.

Layer 1 works on a PC if the sockets are real. Layer 2 works if the credentials
are genuine. Layers 3 and 4 are Meta-side services that a PC application without
Meta's platform client cannot join.

## 3. Where Venture stands, layer by layer

### 3.1 Game transport — real

The Android `libc.so` HLE implements `socket`, `connect`, `bind`, `listen`,
`send`/`recv`, `sendto`/`recvfrom`, `getaddrinfo`, `poll`/`select`/`epoll`,
`eventfd` and `socketpair` over ordinary host sockets
(`src/runtime/hle/libc/net.cpp`), with a non-blocking POSIX backend and a shared
epoll waiter (`src/runtime/platform/posix/libc_net.cpp`) and a Winsock backend
(`src/runtime/platform/win32/libc_net.cpp`). Non-socket Android networking
(`ConnectivityManager`, `NetworkCapabilities`, `NetworkInfo`, `Proxy`,
`TrafficStats`) is emulated under `src/runtime/java/framework/android/net/`.

Consequence: a game's own client can reach its own servers from the PC exactly as
it would from a headset, subject to the game's own platform checks. This is the
single most important fact for interoperability, and it is why the recommended
path requires nothing on the Quest.

### 3.2 Player identity — implemented, live behaviour unproven

`ovr_User_GetAccessToken` performs a real exchange: the user's Meta token is
posted to `/authenticate_application` with the game's app id, the returned token
must differ from the user token and its `app_id` must match, and it is then
validated by reading `/me` with it (`src/runtime/hle/ovrplatform/provider.cpp`,
`authenticate`). `ovr_User_GetUserProof` obtains a single-use nonce from
`/user_nonce_generate`. Entitlement is a real `/entitlements` query for the
signed-in account.

This is the correct, legitimate mechanism: the game sees a genuine Meta-issued,
app-scoped credential for the user's own account — not a fabricated token and not
another user's identity. Whether Meta grants a token with every scope a specific
title's backend demands is only provable with a real account and title; the code
itself notes elsewhere that a returned candidate token is not proof of scope.

### 3.3 Outbound presence and invitations — implemented against Meta's REST API

`ovr_GroupPresence_Set*` stores destination, lobby session id, match session id
and joinability (`session.cpp`, `local_request`), and that state is sent to Meta
in the rich-presence heartbeat's `rich_presence_options`
(`provider.cpp`, `heartbeat`). `ovr_GroupPresence_GetInvitableUsers` and
`ovr_GroupPresence_SendInvites` call the real `/me/application_invitable_users`
and `/me/send_application_invites` endpoints with the app token, including the
lobby/match session ids (`src/runtime/hle/ovrplatform/social.cpp`).

So a game that uses these calls can, in principle, show a Venture player as
online with a joinable session and send a normal Meta invitation to a Quest
friend. The Quest friend receives it through the ordinary Meta platform client
and launches the unmodified game. **Untested**, and it only leads anywhere if the
game itself resolves its session from those lobby/match ids through its own
backend.

### 3.4 Inbound presence and invitations — absent, and the hard blocker

The heartbeat sends `push_token` empty (`provider.cpp`: "No FBNS registration is
implemented"), and the push parser deliberately does not invent notifications
from REST responses (`social.cpp`, closing comment: no GroupPresence join/leave
intent or InvitationsSent source is implemented). `LaunchInvitePanel`,
`LaunchRosterPanel`, `LaunchRejoinDialog` and `LaunchMultiplayerErrorDialog` are
Meta's own compositor UI and are not implemented.

The push channel is device-scoped, not app-scoped: `ovr_PushNotification_Register()`
registers the *device* with Meta's notification service, and delivery is performed
by the Quest system (VrShell) for the installed app. No documented endpoint
accepts a PC registration and delivers the same join intents, and the client REST
surface only *sends* invitations. Server-to-server app credentials exist, but they
must never ship inside a client and would still not make a PC process a
notification target. Meta's PC-side GroupPresence support is exposed through Meta
Horizon Link on a headset-attached PC, not to an arbitrary Linux process.

Consequence: **a Quest player inviting a Venture player from the Quest system UI
cannot complete.** The invitation is delivered to a headset, not to a PC process.
There is no legitimate way to register for Meta's push channel without Meta's
platform client and a registered device, so this is not a bug to be fixed by more
HLE work.

### 3.5 Platform session services — absent, and not reproducible

`ovr_Matchmaking_*`, `ovr_Party_*`, `ovr_NetSync_*` (including platform VoIP) and
the remaining `ovr_GroupPresence_*` UI calls have no handler and complete with the
reserved `unsupported` error (`session.cpp`, worker completion path). These are
server-side Meta services; they cannot be emulated from the client side without
access to Meta's backend, and fabricating them would make games believe in
sessions that do not exist. They stay unsupported.

### 3.6 Session deep links — path exists, entry point added

`LaunchDetails` carries type, deep link, destination, lobby id, match id and room
(`src/common/launch_details.h`), is delivered to the runtime over the launch
channel (`src/runtime/main.cpp`) and is exposed to the game through
`ovr_Application_GetLaunchDetails` (`session.cpp`, `launch_details`). What was
missing is a way to *create* such a launch: the launcher always launched with
default details.

Added in this change: `session_launch_details()` plus
`venture-cli play GAME --deeplink URL --lobby ID --match ID`. A Venture user can
now hand the game the same session identifiers a Quest player's invitation or the
game's share link carries, and the game joins it through its own networking. This
is the only mechanism that lets a Venture player enter an *existing* session
without Venture-specific matchmaking. It is untested against a real title.

Launch type follows Meta's enum: `Normal = 1`, `Invite = 2`, `Deeplink = 4`. An
invitation may also carry a deep link, destination, lobby id and match id; the
type stays `Invite` whenever a lobby or match id is present, because titles branch
on `Invite` to enter a session, while `Deeplink` corresponds specifically to a
launch through `ovr_Application_LaunchOtherApp`. Each value is capped at 4096 bytes
and rejected above that. `ovr_LaunchDetails_GetTrackingID` stays empty: it belongs
to a deeplink flow Venture cannot complete, and inventing one would be worse than
an empty string.

## 4. The three interop tiers

**Tier A — game-native sessions (the recommended path).**
Both players use the game's own join mechanism: a room code, a server browser, or
the game's own friend/invite system in-game. The Quest player installs nothing
extra. Venture's job is only to provide genuine identity, real sockets and a
stable runtime. This is the only tier that can work without Meta cooperation, and
it is where testing effort should go first.

**Tier B — Meta invitations out of Venture (Venture → Quest).**
Implemented at the REST level as described in 3.3. A Quest player can receive a
real invitation and launch the game normally. Requires the title to use
GroupPresence invitations *and* to resolve its session from them. Untested.

**Tier C — Meta platform sessions (Party, Matchmaking, NetSync, inbound invites).**
Blocked. Not implementable from the client. Must be reported as unsupported to
the user rather than approximated.

## 5. Capability matrix

"Source" is what this repository does. "Device" is what has actually been
observed. No device test has been run, so no row is marked working.

| Capability | Source status | Device status |
|---|---|---|
| Venture ↔ Venture, game-native session | Real sockets + real identity; no Venture-specific path needed | Not tested |
| Venture Linux ↔ Venture Windows | Same shared code; POSIX and Winsock socket backends both present | Not tested |
| Venture → native Quest, game-native session (room code / browser / in-game friends) | Expected to work for titles whose backend accepts the app-scoped token from a PC client | **Not tested — requires hardware** |
| Native Quest → Venture-hosted session, via the game's own join mechanism | Same as above, initiated from the Quest side | **Not tested — requires hardware** |
| Venture → native Quest, Meta platform invitation (outbound) | Implemented (`SendInvites`, presence heartbeat) | Not tested |
| Native Quest → Venture, Meta platform invitation (inbound) | Absent: no FBNS push, no join-intent handling | Not possible today |
| Native Quest platform matchmaking (`ovr_Matchmaking_*`) | Absent (`unsupported`) | Not possible |
| Native Quest platform voice (`ovr_NetSync_*` VoIP) | Absent (`unsupported`) | Not possible |
| Game-native voice (Vivox, Photon Voice, in-title chat) | Real sockets; expected to work | Not tested |
| Quest system invite/roster/rejoin panels | Absent (Meta compositor UI) | Not possible |
| Launch into an existing session by deep link / lobby / match id | Implemented in this change; untested | Not tested |
| Same-version requirement | `ovr_Application_GetVersion` reports the installed build; Venture does not claim it is the store's latest | Not tested |

## 6. What cannot be promised

- **"Indistinguishable from a native Quest player" is not achievable at the
  platform level.** Device model, device attestation and the FBNS push channel
  are Meta-side. A title that gates multiplayer on device integrity will refuse a
  PC process; Venture already detects that failure mode and reports it
  (`library::needed_device_attestation`).
- **At the game-backend level it can be indistinguishable**, because the identity
  is genuine and the transport is real. That is the realistic goal.
- **Anti-cheat is independent of all of this.** A title with kernel-level or
  device-integrity anti-cheat will not run, multiplayer or not.
- **Platform matchmaking, party and NetSync voice will not be emulated.** Any
  claim otherwise would be false.

## 7. Game-specific adapters

An adapter is justified only by a *measured* failure in a specific title. Writing
adapters before that produces dead code and false confidence, so the architecture
below is the extension point, not an implementation.

**Shape.** One adapter per package, selected at launch, defaulting to a no-op that
passes everything through:

```cpp
struct MultiplayerContext {
  std::string app_id, package, version_name;
  std::int64_t version_code;
  LaunchDetails launch;          // deeplink / lobby / match ids
  bool quest_interop_expected;   // capability the adapter declares
};

class MultiplayerAdapter {
 public:
  virtual ~MultiplayerAdapter() = default;
  virtual std::string_view id() const = 0;
  // Called before the platform session is created: may adjust launch details
  // (for example map a share link onto a lobby id) or report a hard block.
  virtual void prepare(MultiplayerContext&) {}
  // Declares platform calls this title needs beyond the shared HLE, so the
  // runtime can log a precise diagnosis instead of a generic failure.
  virtual std::span<const std::string_view> required_platform_calls() const { return {}; }
};
```

**Registry.** A validated JSON document keyed by package, loaded once, refusing
unknown fields and unversioned entries:

```json
{
  "version": 1,
  "games": [
    {
      "package": "com.example.game",
      "adapter": "example-room-code",
      "transport": "game_backend",
      "join": "room_code",
      "quest_interop": "expected",
      "verified": false,
      "notes": "Joins by the code the title shows; no platform session calls."
    }
  ]
}
```

**Allowed in an adapter:** session-id mapping, version reporting the title
actually needs, shims for a missing Android API, and diagnostics.
**Never allowed:** fabricating tokens or entitlements, impersonating another
account, bypassing DRM or anti-cheat, or faking a platform session.

**When to write one:** after a real run shows the title failing at a specific
call. The diagnosis comes first, the adapter second.

## 8. Linux notes

- The game's own ports must be reachable, not Venture's. For inbound hosting,
  open the title's UDP/TCP port in the firewall (ufw/firewalld/nftables); for
  direct internet play, forward it or use a VPN mesh. Nothing here is
  SteamVR-specific.
- OpenXR runtime choice (Monado, WiVRn, SteamVR) affects rendering and input
  only; it does not change the game's networking.
- Flatpak builds need network permission; a sandboxed launcher cannot host.
- Wayland, Vulkan and GPU drivers matter for stability, not netcode, but a GPU
  crash during a session looks like a network failure. Read the runtime log before
  blaming the network.
- IPv4 and IPv6 are both available to the game through the socket HLE.

## 9. Test protocol for the critical scenario

This is the test that matters, and it cannot be run in a build container. It needs
a PC with Venture and a real Quest on the same title.

1. Install the same game build on both. Record both version codes
   (`venture-cli info "GAME"`; the Quest shows its version in the library).
2. Player A launches through Venture: `venture-cli play "GAME"` (add
   `--deeplink/--lobby/--match` only if the title's join path requires it).
3. Player B launches the game normally on the Quest.
4. Use the game's own multiplayer UI to create/join. No Venture software on B.
5. Verify *synchronised gameplay*: both players see each other move, and a game
   action by one is visible to the other. A connection alone is not success.
6. If it fails, capture `venture-cli report "GAME" --upload` and note the first
   error in-game. The debug report contains the runtime log, the multiplayer
   diagnostics (`docs/multiplayer-diagnostics.md`) and the exact launch shape.

Record the result per title in the matrix of section 5, with the version codes.

## 10. Roadmap

1. **Multiplayer diagnostics — done in this change.** Per launch, the runtime
   records which platform multiplayer calls the title made (`GroupPresence`,
   `Party`, `Matchmaking`, `NetSync`, `VoIP`, `SendInvites`, launch details),
   whether each is implemented and succeeded, and the title's sockets, connects,
   DNS, timeouts and disconnects. The summary is in the debug report. See
   `docs/multiplayer-diagnostics.md`; fill `docs/multiplayer-compatibility.md`
   from real runs.
2. **Tier A hardware tests** on two or three titles with different transports
   (a room-code title, a server-browser title, a studio-backend title).
3. **Adapters** for the specific calls that fail, in the order the diagnostics
   reveal them.
4. **Tier B validation**: whether Meta accepts the heartbeat/invite calls for a
   PC client, and whether a Quest friend can accept and land in the same session.
5. **Optional coordination layer** (LAN discovery, invitation codes) only for
   titles that have no join mechanism of their own. It is not the path to native
   Quest play and must never be presented as such.
6. **Tier C is closed** unless Meta offers a supported PC-side session API.

## 11. Explicit non-claims

- No game has been run. No Quest has been connected. No Meta account was used.
- The identity exchange, presence heartbeat and invitation calls are implemented
  but have not been observed against Meta's live service in this environment.
- Venture cannot receive Meta platform invitations, cannot join platform
  matchmaking, and cannot provide Meta NetSync voice.
- Nothing in this repository makes a Venture player a Quest device. It makes the
  player a legitimate owner of the title, with a real account and real
  networking, running the title's own multiplayer.
- No per-game compatibility result exists yet. `docs/multiplayer-compatibility.md`
  defines how each title is classified and leaves every cell unmeasured until a
  two-device run fills it.

## 12. Platform API status after this change

Implemented and reachable by a title:

| Surface | State |
|---|---|
| Identity, entitlement, app-token exchange, user nonce | Implemented |
| `ovr_Application_GetLaunchDetails` with Normal/Invite/Deeplink and lobby/match ids | Implemented, getter-tested |
| GroupPresence set/clear destination, lobby and match session | Implemented |
| RichPresence lobby/match session | Implemented |
| `GetInvitableUsers`, `SendInvites` | Implemented against Meta REST (live behaviour unproven) |
| Host networking (TCP/UDP, DNS, IPv6) | Implemented |
| Launch, rejoin and invite panels, party, matchmaking, NetSync, VoIP | Unsupported, reported as such |
| Inbound invitations and join intents | Not possible without Meta's device push channel |

The unimplemented surfaces now report themselves in the diagnostics rather than
failing silently, which is what makes a per-title verdict possible.
