// 持续分配故障注入：对话命令、版本安装、玩家完成、恢复、内外存档和资源解析都不半提交。
#include "test_support.hpp"
#include <designer/dialogue/archive.hpp>
#include <new>
#include <fstream>
#include <iterator>
namespace
{
    long remaining = -1;
}
void *operator new(std::size_t size)
{
    if (remaining == 0)
        throw std::bad_alloc();
    if (remaining > 0)
        --remaining;
    if (void *p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
template <class Operation, class Verify> void sweep(Operation operation, Verify verify)
{
    bool done = false;
    for (long n = 0; n < 20000; ++n)
    {
        remaining = n;
        try
        {
            operation();
            remaining = -1;
            done = true;
        }
        catch (const std::bad_alloc &)
        {
            remaining = -1;
            verify();
        }
        if (done)
            break;
    }
    CHECK(done);
}
int main()
{
    const d::Catalog catalog({dialogue_definition()});
    d::Book book;
    sweep([&] { CHECK(book.execute_dialogue_command(catalog, start()).error == d::Error::none); },
          [&] { CHECK(book.snapshot() == d::Snapshot{}); });
    const auto frozen = book.snapshot();
    d::Book target;
    sweep([&] { CHECK(target.restore(frozen) == d::Error::none); },
          [&] { CHECK(target.snapshot() == d::Snapshot{}); });
    auto v2 = dialogue_definition(2);
    v2.nodes[0].text_key = "new";
    d::Catalog live({dialogue_definition()});
    sweep([&] { live.replace_dialogue_definition(v2); }, [&] { CHECK(live.find(5001)->version == 1); });
    auto p = dialogue_player();
    CHECK(p->accept_quest(1, {5, 0}) == quest::Error::none);
    reach_end(*p);
    const auto before = p->snapshot();
    sweep(
        [&] {
            CHECK(p->execute_dialogue_command(step(103, d::Action::finish, 3, 3)).error == d::Error::none);
        },
        [&] { CHECK(equal(before, p->snapshot())); });
    const auto complete = p->snapshot();
    auto other = dialogue_player();
    const auto empty = other->snapshot();
    sweep([&] { CHECK(other->restore(complete) == quest::Error::none); },
          [&] { CHECK(equal(empty, other->snapshot())); });
    std::string encoded;
    CHECK(player_api::encode_archive(complete, encoded) == player_api::ArchiveError::none);
    std::string output = "keep";
    sweep([&] { CHECK(player_api::encode_archive(complete, output) == player_api::ArchiveError::none); },
          [&] { CHECK(output == "keep"); });
    player_api::PlayerSnapshot decoded;
    sweep([&] { CHECK(player_api::decode_archive(encoded, decoded) == player_api::ArchiveError::none); },
          [&] { CHECK(equal(decoded, player_api::PlayerSnapshot{})); });
    std::string inner;
    CHECK(d::encode_archive(complete.dialogues, inner) == d::ArchiveError::none);
    output = "keep";
    sweep([&] { CHECK(d::encode_archive(complete.dialogues, output) == d::ArchiveError::none); },
          [&] { CHECK(output == "keep"); });
    d::Snapshot decoded_inner;
    sweep([&] { CHECK(d::decode_archive(inner, decoded_inner) == d::ArchiveError::none); },
          [&] { CHECK(decoded_inner == d::Snapshot{}); });
    std::ifstream stream(DESIGNER_DIALOGUE_GRAPH);
    CHECK(stream.good());
    const std::string json{std::istreambuf_iterator<char>(stream), {}};
    sweep([&] { CHECK(cfg::decode_dialogue_content(json).content); }, [] {});
    std::ifstream text_stream(DESIGNER_DIALOGUE_TEXTS);
    CHECK(text_stream.good());
    const std::string texts{std::istreambuf_iterator<char>(text_stream), {}};
    sweep([&] { CHECK(cfg::decode_dialogue_texts(texts).content); }, [] {});
    auto invalid = json;
    invalid.replace(invalid.find("\"next\": 2"), 9, "\"next\": 9");
    sweep([&] { CHECK(cfg::decode_dialogue_content(invalid).error); }, [] {});
    std::cout << "Dialogue allocation failure sweeps passed\n";
}
