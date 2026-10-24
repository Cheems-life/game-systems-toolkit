// 独立资源配置：严格格式/UTF-8/图校验、文本引用、分文件加载和校验后发布，错误不修改旧目录。
#include "core_support.hpp"
#include <designer/configuration/dialogue_content.hpp>
#include <fstream>
#include <iterator>
namespace cfg = designer::configuration;
std::string read(const char *file)
{
    std::ifstream in(file);
    CHECK(in.good());
    return {std::istreambuf_iterator<char>(in), {}};
}
int main()
{
    const auto graph = read(DESIGNER_DIALOGUE_GRAPH), texts = read(DESIGNER_DIALOGUE_TEXTS);
    const auto loaded = cfg::load_dialogue_content(DESIGNER_DIALOGUE_GRAPH);
    CHECK(loaded.content && !loaded.error && loaded.content->definition == dialogue_definition());
    const auto language = cfg::load_dialogue_texts(DESIGNER_DIALOGUE_TEXTS);
    CHECK(language.content && !language.error && language.content->locale == "zh-CN");
    CHECK(!cfg::validate_dialogue_text_references(loaded.content->definition, *language.content));
    auto missing = *language.content;
    missing.texts.erase("answer.yes");
    auto error = cfg::validate_dialogue_text_references(loaded.content->definition, missing, "npc.json");
    CHECK(error && error->code == cfg::Error::reference && error->record_id == 5001 &&
          error->path == "$.dialogue.nodes[1].options[0].text_key");
    auto reject = [&](std::string bytes, cfg::Error code) {
        const auto result = cfg::decode_dialogue_content(bytes, "bad.json");
        CHECK(!result.content && result.error && result.error->code == code &&
              result.error->source == "bad.json");
    };
    auto mutate = [&](const std::string &from, const std::string &to) {
        auto bytes = graph;
        const auto pos = bytes.find(from);
        CHECK(pos != std::string::npos);
        bytes.replace(pos, from.size(), to);
        return bytes;
    };
    reject(mutate("\"version\": 1", "\"version\": 1.0"), cfg::Error::range);
    reject(mutate("\"entry\": 1", "\"entry\": 99"), cfg::Error::rule);
    reject(mutate("\"schema_version\": 1", "\"schema_version\": 2"), cfg::Error::unsupported_version);
    reject(mutate("\"kind\": \"text\"", "\"kind\": \"script\""), cfg::Error::rule);
    reject(mutate("\"id\": 5001", "\"id\": 5001, \"typo\": true"), cfg::Error::structure);
    reject(mutate("\"id\": 5001", "\"id\": 5001, \"id\": 5002"), cfg::Error::duplicate);
    reject(mutate("\"speaker\": 7", "\"speaker\": null"), cfg::Error::range);
    reject(mutate("\"id\": 3, \"kind\": \"end\"", "\"id\": 3, \"kind\": \"end\", \"next\": 1"),
           cfg::Error::structure);
    reject("{}", cfg::Error::structure);
    reject("[]", cfg::Error::structure);
    reject(graph + "x", cfg::Error::syntax);
    reject(std::string(cfg::dialogue_file_max_bytes + 1, ' '), cfg::Error::limit);
    for (const auto &bytes : {std::string("{\"schema_version\":1,\"content_version\":1,\"locale\":\"zh-CN\","
                                          "\"texts\":{\"a\":\"x\",\"a\":\"y\"}}"),
                              std::string("{}")})
        CHECK(cfg::decode_dialogue_texts(bytes).error);
    auto invalid_utf8 = texts;
    invalid_utf8.insert(invalid_utf8.find("你好"), 1, static_cast<char>(0xff));
    CHECK(cfg::decode_dialogue_texts(invalid_utf8).error->code == cfg::Error::syntax);
    auto giant = texts;
    giant.replace(giant.find("你好，旅行者。"), std::string("你好，旅行者。").size(),
                  std::string(cfg::max_text_bytes + 1, 'a'));
    CHECK(cfg::decode_dialogue_texts(giant).error->code == cfg::Error::range);
    const auto missing_file = cfg::load_dialogue_content("/this/path/does/not/exist.json");
    CHECK(!missing_file.content && missing_file.error->code == cfg::Error::io);
    d::Catalog catalog({loaded.content->definition});
    d::Book book;
    CHECK(book.execute_dialogue_command(catalog, start()).error == d::Error::none);
    const auto v2 = cfg::load_dialogue_content(DESIGNER_DIALOGUE_GRAPH_V2);
    CHECK(v2.content && !cfg::validate_dialogue_text_references(v2.content->definition, *language.content));
    catalog.replace_dialogue_definition(v2.content->definition);
    CHECK(catalog.find(5001)->version == 2 && book.query_dialogue_session(100)->definition.version == 1);
    const auto invalid = cfg::decode_dialogue_content(mutate("\"next\": 2", "\"next\": 999"));
    CHECK(invalid.error && catalog.find(5001)->version == 2);
    // 完整 uint64 版本不能经 double 截断。
    const auto huge = cfg::decode_dialogue_content(
        mutate("\"content_version\": 1", "\"content_version\": 18446744073709551615"));
    CHECK(huge.content && huge.content->content_version == UINT64_MAX);
    std::cout << "Split dialogue resources, strict validation and safe publication passed\n";
}
