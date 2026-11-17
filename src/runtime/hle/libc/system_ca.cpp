#include "system_ca.h"
#include "common/log.h"
#include "common/paths.h"
#include <cctype>
#include <mutex>
#include <system_error>

namespace venture::hle::libc::system_ca {
namespace {
bool hashed_name(std::string_view name) {
  if (name.size() < 10 || name[8] != '.') return false;
  for (std::size_t i = 0; i < 8; ++i)
    if (!std::isxdigit(static_cast<unsigned char>(name[i]))) return false;
  for (std::size_t i = 9; i < name.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
  return true;
}
}  // namespace

std::size_t copy_store(const std::filesystem::path& source, const std::filesystem::path& destination) {
  std::error_code ec;
  if (source.empty() || !std::filesystem::is_directory(source, ec)) return 0;
  std::filesystem::create_directories(destination, ec);
  if (ec) {
    log::warn("ca", "Could not create the guest CA store {}: {}", paths::utf8(destination), ec.message());
    return 0;
  }
  std::size_t copied = 0;
  std::filesystem::directory_iterator it(source, std::filesystem::directory_options::skip_permission_denied, ec), end;
  if (ec) {
    log::debug("ca", "Could not read {}: {}", paths::utf8(source), ec.message());
    return 0;
  }
  while (it != end) {
    const auto path = it->path();
    const auto name = paths::utf8(path.filename());
    std::error_code entry_ec;
    if (hashed_name(name) && it->is_regular_file(entry_ec) && !entry_ec) {
      const auto target = destination / path.filename();
      if (!std::filesystem::exists(target, entry_ec) && !entry_ec) {
        if (std::filesystem::copy_file(path, target, std::filesystem::copy_options::none, entry_ec))
          ++copied;
        else if (entry_ec)
          log::debug("ca", "Could not copy root {}: {}", name, entry_ec.message());
      }
    }
    it.increment(ec);
    if (ec) {
      log::debug("ca", "Could not finish reading {}: {}", paths::utf8(source), ec.message());
      break;
    }
  }
  return copied;
}

void ensure(const std::filesystem::path& data_dir) {
  if (data_dir.empty()) return;
  static std::once_flag once;
  std::call_once(once, [&] {
    const auto destination = data_dir / "volumes" / "system" / "etc" / "security" / "cacerts";
    std::size_t copied = 0;
    bool host_store = false;
    for (const auto& source : {"/etc/ssl/certs", "/etc/pki/tls/certs"}) {
      std::error_code ec;
      if (std::filesystem::is_directory(source, ec)) host_store = true;
      copied += copy_store(source, destination);
    }
    if (copied) log::info("ca", "{} root(s) added to /system/etc/security/cacerts", copied);
    else if (!host_store) log::warn("ca", "No host CA directory found for /system/etc/security/cacerts");
  });
}
}  // namespace venture::hle::libc::system_ca
