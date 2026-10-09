#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace designer::persistence {

enum class FileError {
    none, invalid_path, not_found, too_large, io_error, unsupported_platform,
    durability_uncertain
};
struct SaveResult {
    FileError error = FileError::none;
    // True means rename committed, even if syncing the directory then failed.
    bool committed = false;
};
[[nodiscard]] const char* file_error_name(FileError error) noexcept;

// POSIX same-directory atomic replacement, file fsync, then directory fsync.
// Single writer; local filesystem. No automatic backups or crash-orphan cleanup.
[[nodiscard]] SaveResult save_atomic(const std::filesystem::path& path, std::string_view bytes,
                                    std::size_t max_bytes = 16 * 1024 * 1024);
// Bounded read. Output remains unchanged on reported errors.
[[nodiscard]] FileError load_file(const std::filesystem::path& path, std::string& output,
                                 std::size_t max_bytes = 16 * 1024 * 1024);

} // namespace designer::persistence
