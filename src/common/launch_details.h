#pragma once
#include "common/json.h"
#include <cstdint>
#include <string>

namespace venture {
// OVR_LaunchType.h: Normal=1, Invite=2, Coordinated=3, Deeplink=4.
struct LaunchDetails {
  std::int32_t type = 1;
  std::string source = "library", deeplink, destination, lobby, match;
  std::uint64_t room = 0;
};
Json launch_details_json(const LaunchDetails&);
LaunchDetails read_launch_details(const Json&); // bounded; never logs payloads
// A launch that joins an existing game session: the ids a Quest player's
// invitation carries, or the deep link the game itself shares. Only the fields
// given are set, so the game sees exactly the invitation it was handed and
// joins the session through its own networking.
LaunchDetails session_launch_details(std::string deeplink, std::string lobby,
                                     std::string match);
} // namespace venture
