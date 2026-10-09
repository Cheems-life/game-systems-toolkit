#pragma once

#include <designer/inventory/inventory.hpp>
#include <string>
#include <string_view>

namespace designer::inventory {

using ContainerId = std::uint32_t;
struct ContainerSnapshot {
    ContainerId id;
    Snapshot slots;
};
using Archive = std::vector<ContainerSnapshot>;
struct ContainerBinding {
    ContainerId id;
    Inventory* inventory;
};

enum class ArchiveError {
    none, too_large, malformed, unsupported_version, checksum_mismatch,
    duplicate_container, invalid_container, binding_mismatch, invalid_inventory
};
struct ArchiveResult {
    ArchiveError error = ArchiveError::none;
    Error inventory_error = Error::none;
};
[[nodiscard]] const char* archive_error_name(ArchiveError error) noexcept;

// Output arguments and live containers remain unchanged on any reported failure.
// Version 1 is little-endian, bounded, with FNV-1a integrity check (not authentication).
[[nodiscard]] ArchiveResult encode_archive(const Archive& archive, std::string& output);
[[nodiscard]] ArchiveResult decode_archive(std::string_view bytes, Archive& output);
[[nodiscard]] ArchiveResult restore_archive(const Archive& archive,
                                           const std::vector<ContainerBinding>& bindings);

inline constexpr std::size_t archive_max_bytes = 16 * 1024 * 1024;
inline constexpr std::size_t archive_max_containers = 128;
inline constexpr std::size_t archive_max_slots_per_container = 100000;
inline constexpr std::size_t archive_max_total_slots = 200000;

} // namespace designer::inventory
