// 配置加载测试：严格格式、精确整数、资源上限、跨表引用、注册扩展及失败时不发布配置。
#include "test_support.hpp"
#include <limits>
namespace c = designer::configuration;
namespace q = designer::quests;
void reject(const std::string& bytes, c::Error code, const std::string& path = "") {
    const auto result = c::decode(bytes, q::default_objectives(), "bad.json");
    CHECK(!result.bundle && result.error && result.error->code == code);
    CHECK(result.error->source == "bad.json");
    if (!path.empty()) CHECK(result.error->path == path);
}
class CustomFlag final : public q::ObjectiveHandler {
public:
    bool validate(const q::ObjectiveDefinition& d, std::uint64_t p) const noexcept override {
        return d.rule_version == 7 && d.target == 1 && p <= 1;
    }
    bool update(const q::ObjectiveDefinition& d, std::uint64_t p, std::uint64_t input, std::uint64_t& next) const noexcept override {
        if (!validate(d, p) || input != 1) return false;
        next = 1; return true;
    }
    bool complete(const q::ObjectiveDefinition&, std::uint64_t p) const noexcept override { return p == 1; }
};
int main() {
    const auto valid = fixture();
    const auto loaded = c::decode(valid, q::default_objectives());
    CHECK(loaded.bundle && !loaded.error && loaded.bundle->content_version() == 2026102101);
    CHECK(loaded.bundle->items().find(1001)->max_stack == 20);
    CHECK(loaded.bundle->currencies().find(1)->max_balance == 100000);
    CHECK(loaded.bundle->products().find(200) && loaded.bundle->quests().find(2));
    const std::string empty = R"({"schema_version":1,"content_version":1,"items":[],"currencies":[],"products":[],"quests":[]})";
    CHECK(c::decode(empty, q::default_objectives()).bundle);
    reject("", c::Error::syntax);
    const auto syntax = c::decode("{", q::default_objectives()); CHECK(syntax.error->byte != 0);
    reject(valid + "{}", c::Error::syntax);
    reject("{\"items\": [],}", c::Error::syntax);
    reject("{/*comment*/}", c::Error::syntax);
    reject("[]", c::Error::structure);
    reject(replace(valid, "\"schema_version\": 1", "\"schema_version\": 4"), c::Error::unsupported_version, "$.schema_version");
    reject(replace(valid, "\"items\": [", "\"itmes\": ["), c::Error::structure, "$.itmes");
    reject(replace(valid, "\"max_stack\": 20", "\"max_stack\": 20, \"extra\": 1"), c::Error::structure);
    reject(replace(valid, "\"max_stack\": 20", "\"max_stack\": 20, \"max_stack\": 10"), c::Error::duplicate);
    reject(replace(valid, "\"max_stack\": 20", "\"max_stack\": 20, \"max_\\u0073tack\": 10"), c::Error::duplicate);
    reject(replace(valid, "\"content_version\": 2026102101,", ""), c::Error::structure, "$.content_version");
    reject(replace(valid, "\"id\": 1002, \"max_stack\": 10", "\"id\": 1001, \"max_stack\": 10"), c::Error::duplicate);
    for (const std::string n : {"0", "-1", "1.0", "1e0", "true", "null", "\"20\"", "4294967296", "18446744073709551616"})
        reject(replace(valid, "\"max_stack\": 20", "\"max_stack\": " + n), c::Error::range, "$.items[0].max_stack");
    reject(replace(valid, "\"max_balance\": 100000", "\"max_balance\": 1e10000"), c::Error::range);
    const auto full = c::decode(replace(valid, "\"max_balance\": 100000", "\"max_balance\": 18446744073709551615"), q::default_objectives());
    CHECK(full.bundle && full.bundle->currencies().find(1)->max_balance == std::numeric_limits<std::uint64_t>::max());
    reject(replace(valid, "\"available\": true", "\"available\": 1"), c::Error::structure);
    reject(replace(valid, "\"currency\": 1, \"amount\": 100", "\"currency\": 99, \"amount\": 100"), c::Error::reference, "$.products[0].unit_cost.currencies[0].currency");
    reject(replace(valid, "\"item\": 1001, \"quantity\": 5", "\"item\": 9999, \"quantity\": 5"), c::Error::reference, "$.products[0].unit_reward.items[0].item");
    const auto bad_task = replace(valid, "\"item\": 1001, \"quantity\": 2", "\"item\": 9999, \"quantity\": 2");
    reject(bad_task, c::Error::reference, "$.quests[0].reward.items[0].item");
    CHECK(c::decode(bad_task, q::default_objectives()).error->record_id == 1);
    reject(replace(valid, "\"currency\": 1, \"amount\": 100", "\"currency\": 1, \"amount\": 100}, {\"currency\": 1, \"amount\": 1"), c::Error::duplicate);
    reject(replace(valid, "\"type\": 1", "\"type\": 77"), c::Error::rule);
    reject(replace(valid, "\"rule_version\": 1", "\"rule_version\": 99"), c::Error::rule);
    reject(replace(valid, "\"type\": 2, \"rule_version\": 1, \"target\": 1", "\"type\": 2, \"rule_version\": 1, \"target\": 2"), c::Error::rule);
    reject(replace(valid, "\"policy\": \"all\"", "\"policy\": \"any\""), c::Error::rule);
    reject(replace(valid, "\"minimum_level\": 5", "\"minimum_level\": 0"), c::Error::range);
    reject(replace(valid, "\"title\": \"准备出发\"", "\"title\": \"" + std::string(129, 'x') + "\""), c::Error::limit);
    reject(replace(valid, "\"currencies\": [{\"currency\": 1, \"amount\": 100}]", "\"currencies\": []"), c::Error::rule);
    reject(std::string(c::max_bytes + 1, ' '), c::Error::limit);
    auto padded = empty; padded.resize(c::max_bytes, ' ');
    CHECK(c::decode(padded, q::default_objectives()).bundle);
    reject(std::string(32, '[') + "0" + std::string(32, ']'), c::Error::structure);
    reject(std::string(33, '[') + "0" + std::string(33, ']'), c::Error::limit);
    reject(std::string(100, '[') + "0" + std::string(100, ']'), c::Error::limit);
    const auto many = [](std::size_t n, const std::string& row) {
        std::string result;
        for (std::size_t i = 0; i < n; ++i) { if (i != 0) result += ','; result += row; }
        return result;
    };
    for (const auto& entry : std::vector<std::pair<std::string, std::size_t>>{
            {"items", c::max_items}, {"currencies", designer::economy::max_currencies},
            {"products", designer::commerce::max_products}, {"quests", q::max_quests}}) {
        const auto input = replace(empty, "\"" + entry.first + "\":[]", "\"" + entry.first + "\":[" + many(entry.second + 1, "{}") + "]");
        reject(input, c::Error::limit, "$." + entry.first);
    }
    reject(replace(valid, "\"objectives\": [", "\"objectives\": [" + many(33, "{}") + ","), c::Error::limit);
    reject(replace(valid, "\"currencies\": [{\"currency\": 1, \"amount\": 100}]",
        "\"currencies\": [" + many(65, "{}") + "]"), c::Error::limit);
    reject(replace(valid, "\"id\": 20, \"type\": 2", "\"id\": 10, \"type\": 2"), c::Error::duplicate);
    reject(replace(valid, "\"id\": 200, \"version\": 1", "\"id\": 100, \"version\": 1"), c::Error::duplicate);
    reject(replace(valid, "\"id\": 2, \"version\": 1", "\"id\": 1, \"version\": 1"), c::Error::duplicate);
    reject(replace(valid, "\"id\": 2, \"max_balance\": 1000", "\"id\": 1, \"max_balance\": 1000"), c::Error::duplicate);
    reject(replace(valid, "\"currency\": 1, \"amount\": 100}], \"items\": [{\"item\": 1001, \"quantity\": 2",
        "\"currency\": 99, \"amount\": 100}], \"items\": [{\"item\": 1001, \"quantity\": 2"), c::Error::reference);
    const auto inactive = replace(valid, "\"available\": true", "\"available\": false");
    reject(replace(inactive, "\"item\": 1001, \"quantity\": 5", "\"item\": 9999, \"quantity\": 5"), c::Error::reference);
    {
        const std::filesystem::path scratch(DESIGNER_CONFIG_SCRATCH);
        struct Remove { std::filesystem::path path; ~Remove() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{scratch};
        std::ofstream stream(scratch, std::ios::binary); CHECK(stream.good());
        stream << std::string(c::max_bytes + 1, ' '); stream.close(); CHECK(stream.good());
        const auto oversized = c::load(scratch, q::default_objectives());
        CHECK(!oversized.bundle && oversized.error->code == c::Error::limit);
    }
    // 重复对象键检查须能处理嵌套空对象/数组，且对象之间不能共享已用键集合。
    reject(R"({"a":[{}, {"x":1,"x":2}]})", c::Error::duplicate);
    auto registry = q::default_objectives(); registry.add(77, std::make_shared<CustomFlag>());
    const auto custom = replace(valid, "\"type\": 2, \"rule_version\": 1", "\"type\": 77, \"rule_version\": 7");
    const auto custom_bundle = c::decode(custom, registry); CHECK(custom_bundle.bundle);
    q::QuestBook custom_book(custom_bundle.bundle->quests(), custom_bundle.bundle->objectives());
    CHECK(custom_book.accept_quest(1, {5, 0}) == q::Error::none);
    CHECK(custom_book.apply_progress_event({1, {{1, 10, 3}, {1, 20, 1}}}).error == q::Error::none);
    CHECK(custom_book.query_quest_status(1, {5, 1}).status == q::Status::ready);
    reject(custom, c::Error::rule);
    const auto missing = c::load("configuration-file-that-does-not-exist.json", registry);
    CHECK(!missing.bundle && missing.error->code == c::Error::io);
    CHECK(c::load(DESIGNER_CONFIG_FIXTURE, registry).bundle);
    // 坏包独立失败，已成功加载的目录仍可用。
    CHECK(loaded.bundle->products().find(100)->unit_cost.currencies[0].amount == 100);
    // 有界确定性字节扰动：解析器遇到非法 JSON/UTF-8 或非法结构都不能返回半套配置。
    for (std::size_t i = 0; i < valid.size(); i += 7) {
        auto mutated = valid; mutated[i] = static_cast<char>(0xff);
        const auto result = c::decode(mutated, registry);
        CHECK((result.bundle != nullptr) != result.error.has_value());
    }
    std::cout << "Strict configuration validation and registered extensions passed\n";
}
