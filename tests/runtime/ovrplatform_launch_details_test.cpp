// What a game actually receives through the Platform SDK when Venture launches
// it into an existing session. The launcher's mapping is covered by
// launch_channel_test.cpp; this covers delivery through the model getters.
#include "ovrplatform_support.h"

#include <string>

using namespace venture::ovrplatform;
namespace optest = venture::ovrplatform::test;

namespace {
std::string text(Session& session, Handle model, const char* getter) {
  return venture::g2h<char>(session.get(model, getter));
}
}  // namespace

TEST_CASE("An accepted invitation reaches every LaunchDetails getter") {
  optest::Arena arena;
  auto provider = std::make_shared<optest::FakeProvider>();
  Session session(optest::app(), provider, arena.memory());
  REQUIRE(session.initialize(optest::app().app_id) == InitResult::success);

  venture::LaunchDetails details;
  details.type = 2;  // ovrLaunchType_Invite: the user accepted an invitation.
  details.source = "invite";
  details.deeplink = "room=42&from=quest";
  details.destination = "arena";
  details.lobby = "lobby-abc";
  details.match = "match-xyz";
  details.room = 7;
  session.set_launch_details(details);

  const auto launch = session.launch_details();
  REQUIRE(launch);
  CHECK(session.get(launch, "ovr_LaunchDetails_GetLaunchType") == 2);
  CHECK(text(session, launch, "ovr_LaunchDetails_GetLaunchSource") == "invite");
  CHECK(text(session, launch, "ovr_LaunchDetails_GetDeeplinkMessage") == details.deeplink);
  CHECK(text(session, launch, "ovr_LaunchDetails_GetDestinationApiName") == details.destination);
  CHECK(text(session, launch, "ovr_LaunchDetails_GetLobbySessionID") == details.lobby);
  CHECK(text(session, launch, "ovr_LaunchDetails_GetMatchSessionID") == details.match);
  // RoomID is the deprecated numeric room; TrackingID belongs to a deeplink
  // flow Venture cannot complete, so it stays empty rather than invented.
  CHECK(session.get(launch, "ovr_LaunchDetails_GetRoomID") == 7);
  CHECK(text(session, launch, "ovr_LaunchDetails_GetTrackingID").empty());
  // Users is the intended party. Venture knows no other device's users, so the
  // array is empty rather than filled with a guess.
  const auto users = session.get(launch, "ovr_LaunchDetails_GetUsers");
  REQUIRE(users);
  CHECK(session.get(users, "ovr_UserArray_GetSize") == 0);
}

TEST_CASE("A deeplink launch and a library launch report their own type") {
  optest::Arena arena;
  auto provider = std::make_shared<optest::FakeProvider>();
  Session session(optest::app(), provider, arena.memory());
  REQUIRE(session.initialize(optest::app().app_id) == InitResult::success);

  venture::LaunchDetails link;
  link.type = 4;  // ovrLaunchType_Deeplink
  link.source = "deeplink";
  link.deeplink = "game://join?room=7";
  session.set_launch_details(link);
  const auto launch = session.launch_details();
  REQUIRE(launch);
  CHECK(session.get(launch, "ovr_LaunchDetails_GetLaunchType") == 4);
  CHECK(text(session, launch, "ovr_LaunchDetails_GetDeeplinkMessage") == link.deeplink);
  CHECK(text(session, launch, "ovr_LaunchDetails_GetLobbySessionID").empty());
  CHECK(text(session, launch, "ovr_LaunchDetails_GetMatchSessionID").empty());
}

TEST_CASE("Launch details are borrowed once and refuse a second shape") {
  optest::Arena arena;
  auto provider = std::make_shared<optest::FakeProvider>();
  Session session(optest::app(), provider, arena.memory());
  REQUIRE(session.initialize(optest::app().app_id) == InitResult::success);
  session.set_launch_details(venture::LaunchDetails{});
  const auto launch = session.launch_details();
  REQUIRE(launch);
  // The graph is minimized and lives until shutdown; a game reads one object.
  CHECK(session.launch_details() == launch);
  CHECK_THROWS(session.set_launch_details(venture::LaunchDetails{}));
}
