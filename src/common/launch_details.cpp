#include "common/launch_details.h"
#include <stdexcept>
#include <utility>

namespace venture {
Json launch_details_json(const LaunchDetails& d) {
  return {{"type", d.type}, {"source", d.source}, {"deeplink", d.deeplink},
          {"destination", d.destination}, {"lobby", d.lobby}, {"match", d.match}, {"room", d.room}};
}
LaunchDetails read_launch_details(const Json& j) {
  if (!j.is_object()) throw std::runtime_error("Invalid launch details");
  LaunchDetails d;
  const auto& type = j.at("type");
  if (!type.is_number_integer() || type < 1 || type > 4)
    throw std::runtime_error("Invalid launch type");
  d.type = type.get<std::int32_t>();
  for (auto [key, out] : {std::pair{"source", &d.source}, {"deeplink", &d.deeplink},
       {"destination", &d.destination}, {"lobby", &d.lobby}, {"match", &d.match}}) {
    *out = j.at(key).get<std::string>();
    if (out->size() > 4096 || out->find('\0') != out->npos)
      throw std::runtime_error("Invalid launch field");
  }
  if (!j.at("room").is_number_unsigned()) throw std::runtime_error("Invalid launch room");
  d.room = j.at("room").get<std::uint64_t>();
  return d;
}
LaunchDetails session_launch_details(std::string deeplink, std::string lobby,
                                     std::string match) {
  LaunchDetails d;
  const bool session = !lobby.empty() || !match.empty();
  // OVR_LaunchType.h and the Platform SDK reference: Normal=1 is a library
  // launch, Invite=2 is the user accepting an invite (which also carries the
  // deeplink message, destination and lobby/match ids), and Deeplink=4 is
  // specifically a launch from ovr_Application_LaunchOtherApp. An invitation
  // that also carries a deeplink is therefore an Invite, never a Deeplink:
  // games branch on this value to decide whether they were joining a session.
  d.type = session ? 2 : !deeplink.empty() ? 4 : 1;
  // LaunchSource is a free-form developer string ("typically used to
  // distinguish where the deeplink came from"), so these descriptive values
  // cannot conflict with a game's own convention.
  d.source = session ? "invite" : !deeplink.empty() ? "deeplink" : "library";
  d.deeplink = std::move(deeplink);
  d.lobby = std::move(lobby);
  d.match = std::move(match);
  // The same bounds the launch channel and the runtime enforce.
  return read_launch_details(launch_details_json(d));
}
} // namespace venture
