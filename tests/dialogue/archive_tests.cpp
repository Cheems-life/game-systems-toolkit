// 对话存档：独立固定字节样本、冻结图恢复、玩家 v4 事实关联、逐字节截断与带校验和变异。
#include "test_support.hpp"
#include "archive_golden.hpp"
#include <designer/dialogue/archive.hpp>
#include <random>
void seal(std::string &bytes)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < bytes.size() - 8; ++i)
    {
        hash ^= static_cast<unsigned char>(bytes[i]);
        hash *= 1099511628211ull;
    }
    for (unsigned i = 0; i < 8; ++i)
        bytes[bytes.size() - 8 + i] = static_cast<char>((hash >> (8 * i)) & 255);
}
int main()
{
    d::Snapshot fixture;
    CHECK(d::decode_archive(dialogue_golden, fixture) == d::ArchiveError::none);
    CHECK(fixture.sessions.size() == 1 && fixture.sessions[0].status == d::Status::completed &&
          fixture.commands.back().completion_event == 1);
    std::string canonical;
    CHECK(d::encode_archive(fixture, canonical) == d::ArchiveError::none && canonical == dialogue_golden);
    player_api::PlayerSnapshot outer;
    CHECK(player_api::decode_archive(dialogue_player_golden, outer) == player_api::ArchiveError::none);
    CHECK(outer.dialogues == fixture && outer.quests.events[0].fact->subject == 5001);
    CHECK(player_api::encode_archive(outer, canonical) == player_api::ArchiveError::none &&
          canonical == dialogue_player_golden);
    eco::Wallet wallet(eco::CurrencyCatalog({{1, 1000}}));
    auto assets = std::make_unique<eco::Economy>(
        std::move(wallet), std::vector<eco::Container>{{1, inv::Inventory(1, inv::ItemCatalog({{10, 10}}))}});
    player_api::Player legacy(quest::QuestCatalog({}),
                              quest::default_objectives(), std::move(assets));
    CHECK(legacy.restore(outer) == quest::Error::none &&
          legacy.execute_dialogue_command(d::Catalog({}), {11, d::Action::finish, 0, 10, 1, 1}).replayed);
    auto p = dialogue_player();
    CHECK(p->accept_quest(1, {5, 0}) == quest::Error::none);
    reach_end(*p);
    auto saved = p->snapshot();
    std::string inner, bytes;
    CHECK(d::encode_archive(saved.dialogues, inner) == d::ArchiveError::none);
    CHECK(player_api::encode_archive(saved, bytes) == player_api::ArchiveError::none);
    player_api::PlayerSnapshot decoded;
    CHECK(player_api::decode_archive(bytes, decoded) == player_api::ArchiveError::none &&
          equal(decoded, saved));
    auto restored = dialogue_player();
    restored->replace_quests(quest::QuestCatalog({}));
    CHECK(restored->restore(decoded) == quest::Error::none &&
          restored->execute_dialogue_command(d::Catalog({}), step(103, d::Action::finish, 3, 3)).error == d::Error::none);
    for (std::size_t n = 0; n < bytes.size(); ++n)
    {
        CHECK(player_api::decode_archive(std::string_view(bytes).substr(0, n), decoded) !=
                  player_api::ArchiveError::none &&
              equal(decoded, saved));
    }
    d::Snapshot decoded_inner = saved.dialogues;
    for (std::size_t n = 0; n < inner.size(); ++n)
    {
        CHECK(d::decode_archive(std::string_view(inner).substr(0, n), decoded_inner) !=
                  d::ArchiveError::none &&
              decoded_inner == saved.dialogues);
    }
    auto corrupt = inner;
    corrupt[8] = 2;
    seal(corrupt);
    CHECK(d::decode_archive(corrupt, decoded_inner) == d::ArchiveError::unsupported_version);
    corrupt = inner;
    corrupt[12] = static_cast<char>(d::max_sessions + 1);
    seal(corrupt);
    CHECK(d::decode_archive(corrupt, decoded_inner) == d::ArchiveError::malformed);
    corrupt = inner;
    corrupt.insert(corrupt.end() - 8, 'x');
    seal(corrupt);
    CHECK(d::decode_archive(corrupt, decoded_inner) == d::ArchiveError::malformed);
    CHECK(d::decode_archive(std::string(d::archive_max_bytes + 1, 'x'), decoded_inner) ==
          d::ArchiveError::too_large);
    auto bad = saved.dialogues;
    bad.commands[0].sequence = 2;
    std::string keep = "keep";
    CHECK(d::encode_archive(bad, keep) == d::ArchiveError::malformed && keep == "keep");
    std::mt19937 random(20261024);
    for (int n = 0; n < 1000; ++n)
    {
        corrupt = inner;
        corrupt[random() % corrupt.size()] ^= static_cast<char>(1 + random() % 255);
        if (n % 2 == 0)
            seal(corrupt);
        const auto before = decoded_inner;
        const auto result = d::decode_archive(corrupt, decoded_inner);
        if (result != d::ArchiveError::none)
            CHECK(before == decoded_inner);
        else
        {
            CHECK(d::encode_archive(decoded_inner, canonical) == d::ArchiveError::none &&
                  canonical == corrupt);
        }
        corrupt = bytes;
        corrupt[random() % corrupt.size()] ^= static_cast<char>(1 + random() % 255);
        if (n % 2 == 0)
            seal(corrupt);
        const auto previous = decoded;
        const auto result_outer = player_api::decode_archive(corrupt, decoded);
        if (result_outer != player_api::ArchiveError::none)
            CHECK(equal(previous, decoded));
        else
        {
            CHECK(player_api::encode_archive(decoded, canonical) == player_api::ArchiveError::none);
            if (corrupt[8] == 4)
                CHECK(canonical == corrupt);
        }
    }
    std::cout << "Dialogue and player v4 golden archives, truncation and mutations passed\n";
}
