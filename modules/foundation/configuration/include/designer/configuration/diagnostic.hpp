#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
namespace designer::configuration
{
    inline constexpr std::size_t max_bytes = 4 * 1024 * 1024;
    inline constexpr std::size_t max_depth = 32;
    enum class Error
    {
        io,
        syntax,
        structure,
        range,
        duplicate,
        unsupported_version,
        reference,
        rule,
        limit
    };
    [[nodiscard]] inline const char *error_name(Error error) noexcept
    {
        switch (error)
        {
#define CONFIG_ERROR(e)                                                                                      \
    case Error::e:                                                                                           \
        return #e
            CONFIG_ERROR(io);
            CONFIG_ERROR(syntax);
            CONFIG_ERROR(structure);
            CONFIG_ERROR(range);
            CONFIG_ERROR(duplicate);
            CONFIG_ERROR(unsupported_version);
            CONFIG_ERROR(reference);
            CONFIG_ERROR(rule);
            CONFIG_ERROR(limit);
#undef CONFIG_ERROR
        }
        return "unrecognized_error";
    }
    struct Diagnostic
    {
        Error code;
        std::string source;
        std::string path;
        std::optional<std::uint32_t> record_id;
        std::string message;
        std::size_t byte = 0; // 语法错误的 1-based 字节位置；0 表示未提供。
    };

} // namespace designer::configuration
