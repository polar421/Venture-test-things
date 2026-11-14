# Native Quest interoperability test plan

The only test that counts: a Linux Venture player and a normal Meta Quest player,
same game build, playing together through the game's own multiplayer, with
synchronised gameplay. This plan cannot run in a build container; it needs the PC,
the headset and the game.

## Setup

**Player A (Venture)**
- Linux PC with the fork built or installed; CachyOS/KDE Plasma Wayland, Vulkan,
  Monado or WiVRn (SteamVR optional and not required).
- PC VR headset working in the chosen OpenXR runtime.
- The game installed through Venture, launched at least once.
- Meta account signed in, owning the game.

**Player B (Quest)**
- Physical Quest with the game installed from the store.
- No Venture software, no side-loaded build, no developer mode requirement.

**Both**
- The same game version. Read A's with `venture-cli info "GAME"`; read B's in the
  Quest library. A pinned older build on A is a mismatch, not a bug.
- Different Meta accounts (two accounts are what "two players" means).

## Before each test

```sh
venture-cli play "GAME"          # A: starts the game through Venture
venture-cli report "GAME"        # after the run, with the diagnostics section
```

On B, launch the game normally from the Quest library.

## Tests

### 1. Public matchmaking

1. Both choose the game's public/quick match.
2. Record whether they land in the same match; a shared match is a connection,
   not yet a pass — see the sync criterion.

### 2. In-game room joining

1. B creates a private room/lobby in the game and reads the code or name it shows.
2. A enters the same code in the game's own UI.
3. Pass only if both see each other and gameplay synchronises.

This is the highest-value test: it needs nothing from Meta's platform layer.

### 3. Existing session joining

1. B creates a session and, if the game shares a deep link or session id, copy it.
2. On A: `venture-cli play "GAME" --deeplink "<link>"` (or `--lobby` / `--match`
   with the ids the game shows).
3. Pass only if A lands in B's session and gameplay synchronises.

### 4. Invite-based joining, where the game supports it

Two directions, different mechanisms:

- **A invites B (outbound).** A uses the game's invite UI. Venture sends the
  presence heartbeat and the invite through Meta's real API. B accepts on the
  Quest. Record whether the invite arrives, whether accepting launches the game,
  and whether both end in the same session.
- **B invites A (inbound).** Not expected to work: Venture does not register for
  Meta's push channel, so a platform invitation cannot reach the PC. Record the
  failure as "not supported" rather than a defect, and note whether the game
  offers a room code instead.

### 5. Synchronised gameplay (the actual pass criterion)

For every test above, verify all of:

- each player sees the other's avatar/entity moving;
- an action by one (a shot, a pickup, a door, a score) is observed by the other;
- state stays consistent for at least two minutes;
- a late join or a reconnect behaves the same way.

A connection without synchronised gameplay is **not** a pass. Record it as
"connected, not synchronised" and attach both logs.

## What to capture

- `venture-cli report "GAME" --upload` from A. It contains the runtime log and the
  multiplayer diagnostics.
- The game's own log or error text on B, if it shows one.
- Both version codes, the test number, the time, and whether it was LAN or over
  the internet.

## Reading the result with the diagnostics

- No sockets and only `platform_session_intents` → the title is platform-only;
  tests 1–3 cannot work, and that is a title limitation.
- Sockets and connects but `refused`/`timeout` → a network path problem (firewall,
  NAT, server region), not a platform problem.
- Connected, then `disconnects`/`network_errors` → transport established and
  dropped; capture the timestamp against the game's own log.
- `platform_unsupported` for `Matchmaking`/`NetSync` and no sockets → the title
  needs Meta's services; record as structurally incompatible for now.

## Linux notes

- Open only the game's own ports; Venture needs none for this. Check
  `ufw status` / `firewall-cmd --list-all` when connects time out.
- WiVRn/Monado/SteamVR choice does not change the game's networking; a runtime
  crash mid-session looks like a disconnect, so read the runtime log first.
- Flatpak builds need network permission to host or join.
