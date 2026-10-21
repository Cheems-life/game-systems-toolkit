#pragma once

// 启动配置适配器：JSON 不进入领域层；只有完整校验成功才返回只读配置包。
#include <designer/commerce/shop.hpp>
#include <designer/quests/quests.hpp>
#include <designer/interaction/interaction.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace designer::configuration {
inline constexpr std::size_t max_bytes = 4 * 1024 * 1024;
inline constexpr std::size_t max_depth = 32, max_items = 4096;
enum class Error { io, syntax, structure, range, duplicate, unsupported_version, reference, rule, limit };
[[nodiscard]] const char* error_name(Error error) noexcept;
struct Diagnostic {
    Error code;
    std::string source;
    std::string path;
    std::optional<std::uint32_t> record_id;
    std::string message;
    std::size_t byte = 0; // 语法错误的 1-based 字节位置；0 表示未提供。
};

class Bundle;
struct LoadResult {
    std::unique_ptr<const Bundle> bundle;
    std::optional<Diagnostic> error;
};
// 数据失败返回 error + 空包；分配失败等资源异常向上传播。不会修改现有业务对象。
[[nodiscard]] LoadResult decode(std::string_view bytes, const quests::ObjectiveRegistry& registry,
                                std::string_view source = "<memory>");
[[nodiscard]] LoadResult load(const std::filesystem::path& path, const quests::ObjectiveRegistry& registry);

class Bundle {
public:
    [[nodiscard]] std::uint64_t content_version() const noexcept { return version_; }
    [[nodiscard]] const inventory::ItemCatalog& items() const noexcept { return items_; }
    [[nodiscard]] const economy::CurrencyCatalog& currencies() const noexcept { return currencies_; }
    [[nodiscard]] const commerce::ProductCatalog& products() const noexcept { return products_; }
    [[nodiscard]] const quests::QuestCatalog& quests() const noexcept { return quests_; }
    [[nodiscard]] const interaction::Catalog& interactions() const noexcept { return interactions_; }
    [[nodiscard]] const quests::ObjectiveRegistry& objectives() const noexcept { return registry_; }
private:
    friend LoadResult decode(std::string_view, const quests::ObjectiveRegistry&, std::string_view);
    Bundle(std::uint64_t version, inventory::ItemCatalog items, economy::CurrencyCatalog currencies,
           commerce::ProductCatalog products, quests::QuestCatalog quests, quests::ObjectiveRegistry registry, interaction::Catalog interactions);
    std::uint64_t version_;
    inventory::ItemCatalog items_;
    economy::CurrencyCatalog currencies_;
    commerce::ProductCatalog products_;
    quests::QuestCatalog quests_;
    quests::ObjectiveRegistry registry_;
    interaction::Catalog interactions_;
};
} // namespace designer::configuration
