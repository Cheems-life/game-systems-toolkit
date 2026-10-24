// 配置绑定测试：v1/v2 兼容、必填 null/对象、严格枚举和数值、诊断路径及载入后路由。
#include "test_support.hpp"
namespace c = designer::configuration;
namespace q = designer::quests;
std::string bound() {
    auto text = fixture();
    text = replace(text, "\"schema_version\": 1", "\"schema_version\": 2");
    // fixture 中共有三个目标；先全部显式无绑定，再替换指定目标。
    text = replace(text, "\"target\": 3", "\"target\": 3, \"binding\": null");
    text = replace(text, "\"target\": 1}", "\"target\": 1, \"binding\": null}");
    text = replace(text, "\"target\": 10", "\"target\": 10, \"binding\": null");
    return text;
}
int main() {
    const auto blank = bound(); CHECK(c::decode(blank, q::default_objectives()).bundle);
    const auto binding = R"({"fact_type":100,"subject":1001,"input":"quantity"})";
    const auto bytes = replace(blank, "\"binding\": null", std::string("\"binding\": ") + binding);
    const auto loaded = c::decode(bytes, q::default_objectives()); CHECK(loaded.bundle);
    q::QuestBook book(loaded.bundle->quests(), loaded.bundle->objectives()); CHECK(book.accept_quest(1, {5, 0}) == q::Error::none);
    CHECK(book.observe_gameplay_fact({1, 100, 1001, 2}).error == q::Error::none && book.find_accepted_quest(1)->progress[0] == 2);
    CHECK(!loaded.bundle->quests().find(1)->objectives[1].binding);
    CHECK(c::decode(fixture(), q::default_objectives()).bundle); // v1 保持无绑定。
    auto reject = [&](std::string text, c::Error code, std::string path) {
        const auto result = c::decode(text, q::default_objectives());
        CHECK(!result.bundle && result.error && result.error->code == code && result.error->path == path);
        CHECK(result.error->record_id == 1);
    };
    const std::string root = "$.quests[0].objectives[0].binding";
    for (const std::string value : {"-1", "1.5", "1e0", "0", "false", "[]", "\"null\""})
        reject(replace(blank, "\"binding\": null", "\"binding\": " + value), c::Error::structure, root);
    reject(replace(bytes, "\"fact_type\":100", "\"fact_type\":0"), c::Error::range, root + ".fact_type");
    reject(replace(bytes, "\"subject\":1001", "\"subject\":4294967296"), c::Error::range, root + ".subject");
    reject(replace(bytes, "\"input\":\"quantity\"", "\"input\":\"sum\""), c::Error::rule, root + ".input");
    reject(replace(bytes, "\"input\":\"quantity\"", "\"input\":1"), c::Error::rule, root + ".input");
    reject(replace(bytes, "\"subject\":1001,", ""), c::Error::structure, root + ".subject");
    reject(replace(bytes, "\"subject\":1001", "\"subject\":1001,\"extra\":1"), c::Error::structure, root + ".extra");
    reject(replace(blank, ", \"binding\": null", ""), c::Error::structure, root);
    reject(replace(bytes, "\"schema_version\": 2", "\"schema_version\": 1"), c::Error::structure, root);
    const auto one = replace(bytes, "\"input\":\"quantity\"", "\"input\":\"one\""); CHECK(c::decode(one, q::default_objectives()).bundle);
    std::cout << "Versioned binding schemas, strict null distinction and fact routing passed\n";
}
