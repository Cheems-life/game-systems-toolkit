#pragma once

#include <designer/economy/economy.hpp>
#include <string>
#include <string_view>

namespace designer::economy {
enum class ArchiveError { none, too_large, malformed, unsupported_version, checksum_mismatch };
[[nodiscard]] const char* archive_error_name(ArchiveError error) noexcept;
inline constexpr std::size_t archive_max_bytes = 16 * 1024 * 1024;
// Format checks only; current catalog and transaction-history checks happen in Economy::restore.
// Output arguments are only replaced on success. Version 1, bounded LE, FNV-1a (not authentication).
[[nodiscard]] ArchiveError encode_archive(const Snapshot& snapshot, std::string& output);
[[nodiscard]] ArchiveError decode_archive(std::string_view bytes, Snapshot& output);
} // namespace designer::economy
