// A Quest's /system/etc/security/cacerts, populated from this computer's
// trusted roots. Android's native TLS clients ask that directory for
// <subject_hash>.<n> files instead of reading a single CA bundle; without it
// they reject otherwise valid public certificates. EOS's native messaging
// client is one such caller (it failed with TLS alert bad_certificate).
#pragma once
#include <cstddef>
#include <filesystem>
#include <string_view>

namespace venture::hle::libc::system_ca {
inline constexpr std::string_view directory = "/system/etc/security/cacerts";

// Copies every hashed certificate file from `source` into `destination`,
// returning how many files were added. Existing entries are left untouched;
// broken symlinks and unreadable entries are skipped individually.
std::size_t copy_store(const std::filesystem::path& source, const std::filesystem::path& destination);

// Populates <data_dir>/volumes/system/etc/security/cacerts from the host's
// hashed certificate directories once per process. No-op when the host has no
// such directory.
void ensure(const std::filesystem::path& data_dir);
}  // namespace venture::hle::libc::system_ca
