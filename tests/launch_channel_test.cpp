#include <doctest/doctest.h>
#include "common/launch_details.h"
#include "common/process.h"
#include <thread>
#include <future>

#include "async_wait.h"

TEST_CASE("Launcher pipe preserves deeplinks across an inherited child process") {
  const venture::LaunchDetails expected{4, "application", "message with spaces & unicode \xc3\xa9", "destination", "lobby", "match", 17};
  venture::Process process;
  std::promise<venture::Json> received;
  auto request = received.get_future();
  const auto log = venture::fs::temp_directory_path() / ("venture-launch-pipe-" + std::to_string(venture::current_process_id()) + ".log");
  process.spawn(VENTURE_LAUNCH_CHANNEL_CHILD, {}, log, false, [&](const venture::Json& frame) -> venture::Json {
    if (frame.at("op") == "launch_details") return {{"details", venture::launch_details_json(expected)}};
    received.set_value(frame);
    return {{"success", true}};
  });
  const auto ready = request.wait_for(venture::test::hang_timeout);
  INFO("launch request ready=", ready == std::future_status::ready,
       " child exited=", process.poll().has_value());
  REQUIRE(ready == std::future_status::ready);
  const auto frame = request.get();
  CHECK(frame.at("op") == "launch_other_app");
  CHECK(frame.at("app") == "123");
  CHECK(frame.at("details") == venture::launch_details_json(expected));
  REQUIRE(venture::test::wait_until([&] { return process.poll().has_value(); }));
  CHECK(*process.poll() == 0);
  venture::fs::remove(log);
}

TEST_CASE("A session launch carries only the invitation it was given") {
  const auto invite = venture::session_launch_details("", "lobby-1", "");
  CHECK(invite.type == 2);  // OVR_LaunchType.h Invite
  CHECK(invite.source == "invite");
  CHECK(invite.lobby == "lobby-1");
  CHECK(invite.match.empty());
  CHECK(invite.deeplink.empty());

  // An invitation also carries a deep link message; the type stays Invite,
  // because the game branches on it to decide it is joining a session.
  const auto invited = venture::session_launch_details("room=7", "lobby-1", "match-2");
  CHECK(invited.type == 2);
  CHECK(invited.deeplink == "room=7");
  CHECK(invited.lobby == "lobby-1");
  CHECK(invited.match == "match-2");

  const auto link = venture::session_launch_details("game://join?room=7", "", "");
  CHECK(link.type == 4);  // Deeplink: Application_LaunchOtherApp
  CHECK(link.deeplink == "game://join?room=7");
  CHECK(link.source == "invite");

  const auto plain = venture::session_launch_details("", "", "");
  CHECK(plain.type == 1);  // Normal, a library launch
  CHECK(plain.source == "library");

  // The launch channel's bound applies before anything reaches the runtime.
  CHECK_THROWS(venture::session_launch_details(std::string(4097, 'x'), "", ""));
}

TEST_CASE("Launcher frames reject malformed and oversized details") {
  auto json = venture::launch_details_json({});
  json["type"] = 99;
  CHECK_THROWS(venture::read_launch_details(json));
  json["type"] = std::uint64_t{0x100000004};
  CHECK_THROWS(venture::read_launch_details(json));
  json["type"] = 4;
  json["deeplink"] = std::string(4097, 'x');
  CHECK_THROWS(venture::read_launch_details(json));
  auto [a, b] = venture::PipeChannel::pair();
  std::jthread worker([b](std::stop_token stop) { b->serve([](const venture::Json& value) { return value; }, stop); });
  CHECK(a->call({{"op", "first"}}).at("op") == "first");
  CHECK(a->call({{"op", "second"}}).at("op") == "second");
  a->close(); worker.join();
  CHECK_THROWS(a->call({{"op", "closed"}}));
}
