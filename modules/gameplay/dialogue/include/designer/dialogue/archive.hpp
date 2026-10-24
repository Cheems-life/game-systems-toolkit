#pragma once
#include <designer/dialogue/dialogue.hpp>
#include <string_view>
namespace designer::dialogue
{
    inline constexpr std::size_t archive_max_bytes = 4 * 1024 * 1024;
    enum class ArchiveError
    {
        none,
        too_large,
        malformed,
        unsupported_version,
        checksum_mismatch
    };
    [[nodiscard]] const char *archive_error_name(ArchiveError) noexcept;
    // DDLGARCH v1，LE/FNV-1a；错误和分配异常均不覆盖输出。不是身份认证。
    [[nodiscard]] ArchiveError encode_archive(const Snapshot &, std::string &);
    [[nodiscard]] ArchiveError decode_archive(std::string_view, Snapshot &);
} // namespace designer::dialogue
