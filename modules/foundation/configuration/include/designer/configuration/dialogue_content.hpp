#pragma once
// 独立的按文件加载入口；不链接旧资产包、Player 或任务系统。
#include <designer/configuration/diagnostic.hpp>
#include <designer/dialogue/dialogue.hpp>
#include <filesystem>
#include <string_view>
namespace designer::configuration
{
    inline constexpr std::size_t dialogue_file_max_bytes = 512 * 1024, max_texts = 4096,
                                 max_text_bytes = 4096;
    struct DialogueContent
    {
        std::uint64_t content_version;
        dialogue::Definition definition;
    };
    struct TextTable
    {
        std::uint64_t content_version;
        std::string locale;
        std::map<std::string, std::string> texts;
        [[nodiscard]] const std::string *find_text(const std::string &key) const noexcept
        {
            const auto found = texts.find(key);
            return found == texts.end() ? nullptr : &found->second;
        }
    };
    struct DialogueLoadResult
    {
        std::unique_ptr<const DialogueContent> content;
        std::optional<Diagnostic> error;
    };
    struct TextLoadResult
    {
        std::unique_ptr<const TextTable> content;
        std::optional<Diagnostic> error;
    };
    [[nodiscard]] DialogueLoadResult decode_dialogue_content(std::string_view,
                                                             std::string_view source = "<memory>");
    [[nodiscard]] DialogueLoadResult load_dialogue_content(const std::filesystem::path &);
    [[nodiscard]] TextLoadResult decode_dialogue_texts(std::string_view,
                                                       std::string_view source = "<memory>");
    [[nodiscard]] TextLoadResult load_dialogue_texts(const std::filesystem::path &);
    // 发布前对选定语言资源做引用校验；不修改目录或会话。
    [[nodiscard]] std::optional<Diagnostic> validate_dialogue_text_references(
        const dialogue::Definition &, const TextTable &, std::string_view source = "<memory>");
} // namespace designer::configuration
