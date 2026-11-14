#include "common/multiplayer/diagnostics.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace venture::multiplayer {
namespace {
constexpr std::size_t kRecords = 64;  // newest kept per channel
constexpr std::int64_t kFlushIntervalMs = 1000;

struct PlatformRecord {
  std::int64_t at_ms = 0;
  std::string function;
  std::string category;
  bool implemented = false, ok = false, intent = false;
};

struct NetworkRecord {
  std::int64_t at_ms = 0;
  std::string kind;      // socket, connect, dns, disconnect, error
  std::string category;  // fixed outcome name
  int family = 0, type = 0;
  bool ok = false;
};

struct State {
  bool run = false;
  std::string package, app_id, game_version;
  std::filesystem::path directory;
  std::int64_t started_ms = 0, updated_ms = 0, written_ms = 0;
  std::uint32_t process_id = 0;
  bool dirty = false;

  // Platform counters.
  std::uint64_t platform_calls = 0, platform_unsupported = 0, platform_failed = 0,
                platform_session_intents = 0;
  std::int32_t launch_type = 0;
  bool launch_deeplink = false, launch_lobby = false, launch_match = false,
       launch_destination = false;
  std::string launch_source;
  std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> by_operation;

  // Network counters.
  std::uint64_t sockets = 0, sockets_tcp = 0, sockets_udp = 0,
                connect_attempts = 0, connect_successes = 0, connect_failures = 0,
                dns_lookups = 0, dns_failures = 0, timeouts = 0,
                disconnects = 0, network_errors = 0;

  std::deque<PlatformRecord> platform;
  std::deque<NetworkRecord> network;
};

State& state() {
  static State* value = new State();  // never destroyed: late guest threads may record
  return *value;
}

std::mutex& guard() {
  static auto* value = new std::mutex();
  return *value;
}

std::uint32_t process_id() {
#ifdef _WIN32
  return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint32_t>(::getpid());
#endif
}

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
          out += buffer;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

std::string render_locked(const State& s) {
  std::string text;
  text.reserve(8192);
  text += "{\"schema\":1";
  text += ",\"note\":\"No credentials, payloads, addresses or session ids are recorded.\"";
  text += ",\"package\":\"" + escape(s.package) + "\"";
  text += ",\"app_id\":\"" + escape(s.app_id) + "\"";
  text += ",\"game_version\":\"" + escape(s.game_version) + "\"";
  text += ",\"process\":" + std::to_string(s.process_id);
  text += ",\"started_at_ms\":" + std::to_string(s.started_ms);
  text += ",\"updated_at_ms\":" + std::to_string(s.updated_ms);
  text += ",\"counters\":{";
  text += "\"platform_calls\":" + std::to_string(s.platform_calls);
  text += ",\"platform_unsupported\":" + std::to_string(s.platform_unsupported);
  text += ",\"platform_failed\":" + std::to_string(s.platform_failed);
  text += ",\"platform_session_intents\":" + std::to_string(s.platform_session_intents);
  text += ",\"launch_type\":" + std::to_string(s.launch_type);
  text += std::string(",\"launch_has_deeplink\":") + (s.launch_deeplink ? "true" : "false");
  text += std::string(",\"launch_has_lobby\":") + (s.launch_lobby ? "true" : "false");
  text += std::string(",\"launch_has_match\":") + (s.launch_match ? "true" : "false");
  text += std::string(",\"launch_has_destination\":") + (s.launch_destination ? "true" : "false");
  text += ",\"launch_source\":\"" + escape(s.launch_source) + "\"";
  text += ",\"sockets\":" + std::to_string(s.sockets);
  text += ",\"sockets_tcp\":" + std::to_string(s.sockets_tcp);
  text += ",\"sockets_udp\":" + std::to_string(s.sockets_udp);
  text += ",\"connect_attempts\":" + std::to_string(s.connect_attempts);
  text += ",\"connect_successes\":" + std::to_string(s.connect_successes);
  text += ",\"connect_failures\":" + std::to_string(s.connect_failures);
  text += ",\"dns_lookups\":" + std::to_string(s.dns_lookups);
  text += ",\"dns_failures\":" + std::to_string(s.dns_failures);
  text += ",\"timeouts\":" + std::to_string(s.timeouts);
  text += ",\"disconnects\":" + std::to_string(s.disconnects);
  text += ",\"network_errors\":" + std::to_string(s.network_errors);
  text += "}";
  text += ",\"by_operation\":{";
  bool first = true;
  for (const auto& [name, counts] : s.by_operation) {
    if (!first) text += ',';
    first = false;
    text += "\"" + escape(name) + "\":{\"calls\":" + std::to_string(counts.first) +
            ",\"ok\":" + std::to_string(counts.second) + "}";
  }
  text += "}";
  text += ",\"platform\":[";
  bool first_platform = true;
  for (const auto& r : s.platform) {
    if (!first_platform) text += ',';
    first_platform = false;
    text += "{\"t\":" + std::to_string(r.at_ms);
    text += ",\"op\":\"" + escape(r.function) + "\"";
    text += std::string(",\"implemented\":") + (r.implemented ? "true" : "false");
    text += std::string(",\"ok\":") + (r.ok ? "true" : "false");
    text += ",\"category\":\"" + escape(r.category) + "\"";
    text += std::string(",\"session_intent\":") + (r.intent ? "true" : "false") + "}";
  }
  text += "],\"network\":[";
  bool first_network = true;
  for (const auto& r : s.network) {
    if (!first_network) text += ',';
    first_network = false;
    text += "{\"t\":" + std::to_string(r.at_ms);
    text += ",\"kind\":\"" + escape(r.kind) + "\"";
    text += ",\"category\":\"" + escape(r.category) + "\"";
    text += ",\"family\":" + std::to_string(r.family);
    text += ",\"type\":" + std::to_string(r.type);
    text += std::string(",\"ok\":") + (r.ok ? "true" : "false") + "}";
  }
  text += "]}";
  return text;
}

void write_locked(State& s) {
  if (s.directory.empty()) return;
  const auto file = s.directory / "multiplayer-diagnostics.json";
  const auto temporary = s.directory / "multiplayer-diagnostics.json.tmp";
  const auto text = render_locked(s);
  std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
  if (!out) return;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.close();
  if (!out) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return;
  }
  std::error_code error;
  std::filesystem::rename(temporary, file, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return;
  }
  s.written_ms = now_ms();
  s.dirty = false;
}

void maybe_write_locked(State& s, std::int64_t now) {
  if (!s.run || s.directory.empty() || !s.dirty) return;
  if (now - s.written_ms < kFlushIntervalMs) return;
  s.updated_ms = now;
  write_locked(s);
}

void push_platform_locked(State& s, PlatformRecord record) {
  if (s.platform.size() >= kRecords) s.platform.pop_front();
  s.platform.push_back(std::move(record));
}

void push_network_locked(State& s, NetworkRecord record) {
  if (s.network.size() >= kRecords) s.network.pop_front();
  s.network.push_back(std::move(record));
}
}  // namespace

bool tracked(std::string_view function) {
  static constexpr std::string_view prefixes[] = {"ovr_GroupPresence_", "ovr_Matchmaking_",
                                                  "ovr_Party_", "ovr_NetSync_", "ovr_Voip_"};
  for (auto prefix : prefixes)
    if (function.starts_with(prefix)) return true;
  return function == "ovr_RichPresence_SetLobbySession" ||
         function == "ovr_RichPresence_SetMatchSession" ||
         function == "ovr_Application_LaunchOtherApp";
}

bool session_intent(std::string_view function) {
  // Joining or advertising a session. A GroupPresence update makes the user
  // joinable; matchmaking, party and NetSync are how a game asks for a session.
  // None of these proves a socket was opened; the network records are that.
  if (function.starts_with("ovr_GroupPresence_Set") || function == "ovr_GroupPresence_Clear")
    return true;
  if (function == "ovr_Matchmaking_CreateRoom" || function == "ovr_Matchmaking_CreateRoom2" ||
      function == "ovr_Matchmaking_CreateAndEnqueueRoom" ||
      function == "ovr_Matchmaking_CreateAndEnqueueRoom2" ||
      function == "ovr_Matchmaking_Enqueue" || function == "ovr_Matchmaking_Enqueue2" ||
      function == "ovr_Matchmaking_EnqueueRoom" || function == "ovr_Matchmaking_EnqueueRoom2" ||
      function == "ovr_Matchmaking_JoinRoom" || function == "ovr_Matchmaking_Browse" ||
      function == "ovr_Matchmaking_Browse2")
    return true;
  if (function == "ovr_Party_Create" || function == "ovr_Party_GatherInApplication")
    return true;
  if (function == "ovr_NetSync_Connect" || function == "ovr_NetSync_Disconnect")
    return true;
  return function == "ovr_Application_LaunchOtherApp";
}

void begin_run(std::string package, std::string app_id, std::string game_version,
               std::filesystem::path directory) {
  std::lock_guard lock(guard());
  State& s = state();
  s = State{};
  s.run = true;
  s.package = std::move(package);
  s.app_id = std::move(app_id);
  s.game_version = std::move(game_version);
  s.directory = std::move(directory);
  s.process_id = process_id();
  s.started_ms = s.updated_ms = now_ms();
  // Write the empty record at once, so a run that made no multiplayer calls is
  // reported as zero rather than looking like missing diagnostics.
  if (!s.directory.empty()) write_locked(s);
}

void note_platform_call(std::string_view function, bool implemented, bool ok,
                        std::string_view category, bool intent) {
  if (!tracked(function)) return;
  std::lock_guard lock(guard());
  State& s = state();
  ++s.platform_calls;
  if (!implemented) ++s.platform_unsupported;
  if (!ok) ++s.platform_failed;
  if (intent) ++s.platform_session_intents;
  auto& counts = s.by_operation[std::string(function)];
  ++counts.first;
  if (ok) ++counts.second;
  push_platform_locked(s, {now_ms(), std::string(function), std::string(category),
                           implemented, ok, intent});
  s.dirty = true;
  maybe_write_locked(s, now_ms());
}

void note_launch_details(std::int32_t type, bool deeplink, bool lobby, bool match,
                         bool destination, std::string_view source) {
  std::lock_guard lock(guard());
  State& s = state();
  // The launch shape belongs to the run even if the game never calls a
  // multiplayer API afterwards, so it is kept as counters, not a record.
  s.launch_type = type;
  s.launch_deeplink = deeplink;
  s.launch_lobby = lobby;
  s.launch_match = match;
  s.launch_destination = destination;
  s.launch_source = std::string(source);
  s.updated_ms = now_ms();
  s.dirty = true;
  maybe_write_locked(s, now_ms());
}

void note_socket(int family, int type) {
  std::lock_guard lock(guard());
  State& s = state();
  ++s.sockets;
  if (type == 1) ++s.sockets_tcp;
  if (type == 2) ++s.sockets_udp;
  push_network_locked(s, {now_ms(), "socket", "", family, type, true});
  s.dirty = true;
  maybe_write_locked(s, now_ms());
}

void note_connect(int family, bool ok, std::string_view category) {
  std::lock_guard lock(guard());
  State& s = state();
  ++s.connect_attempts;
  if (ok) ++s.connect_successes;
  else ++s.connect_failures;
  if (category == "timeout") ++s.timeouts;
  push_network_locked(s, {now_ms(), "connect", std::string(category), family, 0, ok});
  s.dirty = true;
  maybe_write_locked(s, now_ms());
}

void note_dns(bool ok, int family) {
  std::lock_guard lock(guard());
  State& s = state();
  ++s.dns_lookups;
  if (!ok) ++s.dns_failures;
  push_network_locked(s, {now_ms(), "dns", ok ? "" : "lookup_failed", family, 0, ok});
  s.dirty = true;
  maybe_write_locked(s, now_ms());
}

void note_disconnect(std::string_view category) {
  std::lock_guard lock(guard());
  State& s = state();
  ++s.disconnects;
  push_network_locked(s, {now_ms(), "disconnect", std::string(category), 0, 0, false});
  s.dirty = true;
  maybe_write_locked(s, now_ms());
}

void note_network_error(std::string_view category) {
  std::lock_guard lock(guard());
  State& s = state();
  ++s.network_errors;
  push_network_locked(s, {now_ms(), "error", std::string(category), 0, 0, false});
  s.dirty = true;
  maybe_write_locked(s, now_ms());
}

std::string json() {
  std::lock_guard lock(guard());
  return render_locked(state());
}

std::filesystem::path report_path() {
  std::lock_guard lock(guard());
  const State& s = state();
  return s.directory.empty() ? std::filesystem::path{}
                             : s.directory / "multiplayer-diagnostics.json";
}

void flush() {
  std::lock_guard lock(guard());
  State& s = state();
  s.updated_ms = now_ms();
  write_locked(s);
}

void reset() {
  std::lock_guard lock(guard());
  state() = State{};
}

}  // namespace venture::multiplayer
