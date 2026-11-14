#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "common/multiplayer/diagnostics.h"

namespace diag = venture::multiplayer;

TEST_CASE("Only multiplayer platform operations are recorded") {
  CHECK(diag::tracked("ovr_GroupPresence_SetLobbySession"));
  CHECK(diag::tracked("ovr_GroupPresence_SendInvites"));
  CHECK(diag::tracked("ovr_Matchmaking_Enqueue"));
  CHECK(diag::tracked("ovr_NetSync_Connect"));
  CHECK(diag::tracked("ovr_Voip_SetMicrophoneMuted"));
  CHECK(diag::tracked("ovr_Party_Create"));
  CHECK(diag::tracked("ovr_Application_LaunchOtherApp"));
  // Identity, entitlement and IAP are not multiplayer operations, and their
  // names must never appear in a report a player uploads.
  CHECK_FALSE(diag::tracked("ovr_User_GetAccessToken"));
  CHECK_FALSE(diag::tracked("ovr_User_GetUserProof"));
  CHECK_FALSE(diag::tracked("ovr_Entitlement_GetIsViewerEntitled"));
  CHECK_FALSE(diag::tracked("ovr_IAP_GetViewerPurchases"));
  CHECK_FALSE(diag::tracked(""));
}

TEST_CASE("Session-establishing operations are distinguished from reads") {
  CHECK(diag::session_intent("ovr_Matchmaking_Enqueue"));
  CHECK(diag::session_intent("ovr_Matchmaking_JoinRoom"));
  CHECK(diag::session_intent("ovr_NetSync_Connect"));
  CHECK(diag::session_intent("ovr_Party_GatherInApplication"));
  CHECK(diag::session_intent("ovr_GroupPresence_SetLobbySession"));
  // Reading the invite list and sending outro invites is not joining anything.
  CHECK_FALSE(diag::session_intent("ovr_GroupPresence_GetInvitableUsers"));
  CHECK_FALSE(diag::session_intent("ovr_GroupPresence_SendInvites"));
  CHECK_FALSE(diag::session_intent("ovr_Matchmaking_GetStats"));
}

TEST_CASE("Counters separate implemented, unsupported and failed operations") {
  diag::reset();
  diag::begin_run("com.example.game", "123", "1.2.3", {});
  diag::note_platform_call("ovr_Matchmaking_Enqueue", false, false, "unsupported", true);
  diag::note_platform_call("ovr_Matchmaking_Enqueue", false, false, "unsupported", true);
  diag::note_platform_call("ovr_GroupPresence_Set", true, true, "none", true);
  diag::note_platform_call("ovr_GroupPresence_Set", true, false, "unavailable", true);
  // Not a multiplayer operation: no counter may move.
  diag::note_platform_call("ovr_User_GetAccessToken", true, true, "none", false);

  const auto doc = nlohmann::json::parse(diag::json());
  CHECK(doc.at("counters").at("platform_calls") == 4);
  CHECK(doc.at("counters").at("platform_unsupported") == 2);
  CHECK(doc.at("counters").at("platform_failed") == 3);
  CHECK(doc.at("counters").at("platform_session_intents") == 4);
  CHECK(doc.at("by_operation").at("ovr_Matchmaking_Enqueue").at("calls") == 2);
  CHECK(doc.at("by_operation").at("ovr_Matchmaking_Enqueue").at("ok") == 0);
  CHECK(doc.at("by_operation").at("ovr_GroupPresence_Set").at("ok") == 1);
  CHECK_FALSE(doc.at("by_operation").contains("ovr_User_GetAccessToken"));
}

TEST_CASE("The launch shape is recorded as presence, never as values") {
  diag::reset();
  diag::begin_run("com.example.game", "123", "1.2.3", {});
  diag::note_launch_details(2, true, true, false, true, "invite");
  const auto doc = nlohmann::json::parse(diag::json());
  CHECK(doc.at("counters").at("launch_type") == 2);
  CHECK(doc.at("counters").at("launch_has_deeplink") == true);
  CHECK(doc.at("counters").at("launch_has_lobby") == true);
  CHECK(doc.at("counters").at("launch_has_match") == false);
  CHECK(doc.at("counters").at("launch_has_destination") == true);
  CHECK(doc.at("counters").at("launch_source") == "invite");
  // There is no field in the document that could hold a session id or a link.
  CHECK_FALSE(diag::json().find("lobby-abc") != std::string::npos);
}

TEST_CASE("Network counters and records are bounded") {
  diag::reset();
  diag::begin_run("com.example.game", "123", "1.2.3", {});
  diag::note_socket(2, 2);   // UDP
  diag::note_socket(10, 1);  // TCP
  diag::note_connect(2, true, "connected");
  diag::note_connect(2, false, "timeout");
  diag::note_dns(false, 2);
  diag::note_disconnect("peer_closed");
  diag::note_network_error("send_failed");
  for (int i = 0; i < 200; ++i) diag::note_connect(2, false, "refused");

  const auto doc = nlohmann::json::parse(diag::json());
  const auto counters = doc.at("counters");
  CHECK(counters.at("sockets") == 2);
  CHECK(counters.at("sockets_udp") == 1);
  CHECK(counters.at("sockets_tcp") == 1);
  CHECK(counters.at("connect_attempts") == 202);
  CHECK(counters.at("connect_successes") == 1);
  CHECK(counters.at("connect_failures") == 201);
  CHECK(counters.at("timeouts") == 1);
  CHECK(counters.at("dns_lookups") == 1);
  CHECK(counters.at("dns_failures") == 1);
  CHECK(counters.at("disconnects") == 1);
  CHECK(counters.at("network_errors") == 1);
  CHECK(doc.at("network").size() == 64);
}

TEST_CASE("A report is written atomically only when a directory is set") {
  diag::reset();
  diag::begin_run("com.example.game", "123", "1.2.3", {});
  CHECK(diag::report_path().empty());
  diag::note_socket(2, 2);
  diag::flush();
  CHECK(diag::report_path().empty());

  const auto directory =
      std::filesystem::temp_directory_path() /
      ("venture-multiplayer-diagnostics-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  diag::reset();
  diag::begin_run("com.example.game", "123", "1.2.3", directory);
  diag::note_socket(2, 2);
  diag::flush();
  const auto file = diag::report_path();
  REQUIRE(std::filesystem::exists(file));
  std::ifstream in(file, std::ios::binary);
  const auto text = std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  const auto doc = nlohmann::json::parse(text);
  CHECK(doc.at("schema") == 1);
  CHECK(doc.at("package") == "com.example.game");
  CHECK(doc.at("note").get<std::string>().find("No credentials") != std::string::npos);
  std::filesystem::remove_all(directory, error);
  diag::reset();
}
