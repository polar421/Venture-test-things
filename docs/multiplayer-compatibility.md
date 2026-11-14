# Per-game multiplayer compatibility

No title has been measured in the environment this work was written in: there is
no Quest headset, no game build and no Meta account there. This document defines
how each title is classified, what evidence decides it, and the table to fill in.
**Empty cells mean unmeasured. They are not "no".** Nothing may be marked working
without a two-device test that shows synchronised gameplay.

## Why titles differ

Each title combines four layers independently:

1. **Transport** — its own backend (Photon, PlayFab/UGS, EOS, a studio server,
   direct IP) or Meta's services.
2. **Session discovery** — an in-game room code, a server browser, the game's own
   friend list, or Meta's GroupPresence/Party UI.
3. **Platform dependency** — what it asks the Platform SDK for before it will
   enter multiplayer: identity, entitlement, GroupPresence, invites, matchmaking,
   NetSync, device attestation.
4. **Version policy** — whether it refuses a peer on another build.

Venture supplies layer 1 (real host sockets), layer 2 only insofar as the game's
own UI provides it, and part of layer 3 (real identity, entitlement, presence
heartbeat, outbound invites; no matchmaking, party, NetSync or inbound invites).

## Classification

| Field | Values | Decided by |
|---|---|---|
| `transport` | `own_backend`, `third_party`, `platform`, `direct`, `unknown` | Diagnostics: sockets/connects present or absent; platform calls present |
| `join` | `room_code`, `server_browser`, `in_game_friends`, `platform_invite`, `public_matchmaking` | The game's own UI, observed once |
| `platform_calls` | the operations the diagnostics recorded | `multiplayer-diagnostics.json` |
| `platform_only` | yes when `platform_session_intents > 0` and `connect_attempts == 0` | Diagnostics |
| `venture_to_quest` | `untested`, `works`, `fails` | Two-device test |
| `quest_to_venture` | `untested`, `works`, `fails` | Two-device test |
| `sync_verified` | yes only when both players see each other's actions | Two-device test |

A title is **structurally incompatible** when it has no join path except Meta
matchmaking, party or NetSync, when it refuses the client on device attestation,
or when anti-cheat rejects the environment. That is a conclusion about the title,
not a bug to fix in the emulator, and it should be recorded as such.

## Table

| Title | Version | Transport | Join | Platform calls seen | Venture → Quest | Quest → Venture | Sync verified |
|---|---|---|---|---|---|---|---|
| _(none measured)_ | | | | | | | |

Fill one row per title after a real run. Keep the version codes of both sides:
a version mismatch is the first thing to rule out.

## What the diagnostics answer, and what they cannot

Answered per launch:

- which multiplayer platform calls the title makes, and which are unimplemented
- whether the title opened sockets, resolved names, connected, and where it failed
- whether an invitation launch was handed to it, and of what shape

Not answered:

- whether a remote peer accepted the connection (needs the other device)
- whether in-game state stayed in sync (needs both players)
- which server the game talked to (addresses are deliberately not recorded)
- why a title's backend refused a client (server-side; the title's own log)

## Suggested order

Start with titles most likely to work: those with an in-game room code or server
browser, and whose diagnostics show `connect_attempts > 0` and no
`platform_session_intents`. Leave titles whose only join path is Meta
matchmaking for last: they need a platform capability that cannot be reproduced
on a PC.
