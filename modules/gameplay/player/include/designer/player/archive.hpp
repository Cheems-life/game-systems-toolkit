#pragma once
#include <designer/player/player.hpp>
#include <string_view>

namespace designer::player
{
    enum class ArchiveError
    {
        none,
        too_large,
        malformed,
        unsupported_version,
        checksum_mismatch
    };
    inline constexpr std::size_t archive_max_bytes = 16 * 1024 * 1024;

    [[nodiscard]] const char *archive_error_name(ArchiveError) noexcept;

    // DQSTARCH v1, bounded LE with FNV-1a integrity check (not authentication).
    // Structural validation here; registered objective rules and progress reconstruction in Player::restore.
    // Output remains unchanged on reported failures and allocation exceptions.

    [[nodiscard]] ArchiveError encode_archive(const PlayerSnapshot &, std::string &output);
    [[nodiscard]] ArchiveError decode_archive(std::string_view bytes, PlayerSnapshot &output);

} // namespace designer::player
