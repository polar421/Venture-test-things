#pragma once
// What a game tried to do with Quest multiplayer, and what the emulator did
// with it.
//
// The recorder is built so that sensitive values cannot enter it: its API only
// accepts operation names, fixed outcome categories, booleans, socket families
// and types. There is deliberately no parameter for a token, a user proof, a
// URL, an address, a session id or a payload, so no caller can leak one by
// mistake. A game's own session ids and deep links are reduced to "present".
//
// Recording is cheap: counters are updated under one short lock only for
// multiplayer platform calls and connection-level socket events, not for
// per-frame reads and writes. The report file is rewritten at most once a
// second, only when something changed.
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace venture::multiplayer {

// Whether this platform function belongs to the multiplayer surface this
// report follows (GroupPresence, Matchmaking, Party, NetSync, VoIP, invites and
// app-to-app launches). Identity, entitlement and IAP calls are not recorded.
bool tracked(std::string_view function);

// Whether the function is how a game joins or advertises a session. This is a
// property of the call, not evidence that a socket was opened; the network
// records below are that evidence.
bool session_intent(std::string_view function);

// Starts a run's record. `directory` is where the report is written (the data
// folder); empty disables writing, which tests use. Called once the runtime
// knows the app identity.
void begin_run(std::string package, std::string app_id, std::string game_version,
               std::filesystem::path directory);

void note_platform_call(std::string_view function, bool implemented, bool ok,
                        std::string_view category, bool session_intent);

// The launch a game was handed, by shape only. Never the ids or the deep link.
void note_launch_details(std::int32_t type, bool deeplink, bool lobby, bool match,
                         bool destination, std::string_view source);

// Guest networking, from the libc socket HLE. `family` is the guest value
// (2 = AF_INET, 10 = AF_INET6); `type` is SOCK_STREAM/SOCK_DGRAM.
void note_socket(int family, int type);
void note_connect(int family, bool ok, std::string_view category);
void note_dns(bool ok, int family);
void note_disconnect(std::string_view category);
void note_network_error(std::string_view category);

// The report document (schema-versioned JSON) and its path, if writing is on.
std::string json();
std::filesystem::path report_path();
void flush();
// Clears everything, including the run context. Tests only.
void reset();

}  // namespace venture::multiplayer
