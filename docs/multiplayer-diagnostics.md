# Multiplayer diagnostics

Every game launch now records which Quest multiplayer APIs the game called, what
the emulator did with each, and what the game's own networking did. The record is
written beside Venture's other data files and included in the normal debug report,
so one hardware test produces evidence instead of an anecdote.

It is evidence of what the game *attempted*. It is not proof that multiplayer
worked, and nothing here is marked as working without a real two-device test.

## What is recorded

Platform operations (only these surfaces; identity, entitlement and IAP are
deliberately excluded):

- `ovr_GroupPresence_*` — setting a destination, lobby id, match id, joinability;
  the invite panel, roster panel, rejoin dialog, invitable users and sending invites.
- `ovr_Matchmaking_*` — browse, create room, enqueue, join, start match, stats.
- `ovr_Party_*` — party creation and gathering in an application.
- `ovr_NetSync_*` — Meta's positional voice and state transport.
- `ovr_Voip_*` — the VoIP surface.
- `ovr_RichPresence_SetLobbySession` / `SetMatchSession` and
  `ovr_Application_LaunchOtherApp`.

For each: function name, whether the emulator implements it, whether it
succeeded, a fixed error category, a timestamp, the game process id, and whether
the call is one of the ways a game joins or advertises a session.

The launch itself is recorded by shape only: launch type (Normal/Invite/Deeplink),
and whether a deep link, lobby id, match id, destination and source were present.

Networking, from the guest's own socket calls:

| Recorded | Source |
|---|---|
| Socket created, with protocol family and stream/datagram | `socket` |
| Connection attempt, success/failure, family, category (`timeout`, `refused`, `unreachable`) | `connect` |
| DNS lookup and lookup failure | `getaddrinfo` |
| Disconnect (`peer_closed`, `write_closed`) | `recv` EOF, `send` EPIPE/ECONNRESET |
| Send failure | `send` errors |
| Timeout counter | connect `ETIMEDOUT` |

## What is never recorded

The recorder's API has no parameter for a token, user proof, password, cookie,
authorization header, account or user id, URL, IP address, port, session id or
payload. That is structural, not a filtering pass: a caller cannot pass a secret
because there is nowhere to put it. Session ids and deep links are reduced to
"present" booleans, and response bodies never reach the recorder.

The report file says so in its own `note` field, so a reader of an uploaded report
can see the guarantee without reading this code.

## Where it is

- Runtime: `<Venture data folder>/multiplayer-diagnostics.json`, rewritten at
  most once a second while something changes, and once at startup so a run with
  zero calls still says zero.
- Report: `venture-cli report GAME` (or **Report issue** in the launcher) adds a
  **Multiplayer diagnostics** section with the file's contents.

## Reading a record

- `platform_unsupported > 0` — the game called a Meta service Venture does not
  implement (usually matchmaking, party or NetSync). That title cannot reach the
  platform session layer; check whether it also tried its own networking.
- `platform_session_intents > 0` with `connect_attempts == 0` — the game asked
  Meta for a session and never opened a socket. Its multiplayer is platform-only.
- `connect_attempts > 0` — the game used its own networking. Successes,
  `dns_failures`, `timeouts` and `refused` say where it stopped. Addresses are
  not recorded by design; use `VENTURE_TRACE_NET=1` in the runtime for a local,
  unuploaded address trace when debugging your own setup.
- `network_errors` / `disconnects` rising mid-session — the transport worked and
  then dropped; distinct from never connecting.
- `launch_type == 2` with `launch_has_lobby`/`launch_has_match` — the game was
  handed an invitation; whether it used it is visible in the calls that follow.

The `session_intent` flag is a property of the API call, not proof that a socket
was opened. The network counters are that evidence. The report does not claim
per-call causality between a platform call and a connection.

## Limits

- One file per launch, overwritten by the next launch of the same game.
- The newest 64 platform and 64 network records are kept; counters are complete.
- A crash loses at most the last second of records. The runtime log still holds
  the platform request lines, which the debug report also includes.
- Recording costs a short lock on rare events only (socket creation, connect,
  DNS, errors, platform calls), never per frame or per packet.
