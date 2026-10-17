#pragma once

#include <designer/commerce/shop.hpp>
#include <string>
#include <string_view>

namespace designer::commerce {
enum class ArchiveError { none, too_large, malformed, unsupported_version, checksum_mismatch };
[[nodiscard]] const char* archive_error_name(ArchiveError error) noexcept;
inline constexpr std::size_t archive_max_bytes = 16 * 1024 * 1024;
// Version 1; LE, bounded, FNV-1a. Validates receipt links, not current catalog rules or authenticity.
// Reported failures and allocation exceptions preserve output arguments.
[[nodiscard]] ArchiveError encode_archive(const Snapshot& snapshot, std::string& output);
[[nodiscard]] ArchiveError decode_archive(std::string_view bytes, Snapshot& output);
} // namespace designer::commerce
