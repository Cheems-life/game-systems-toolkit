// 玩家任务格式测试：统一资产/购买/任务往返、损坏与非法关联、恢复规则及字节变异。
#include "quest_test_support.hpp"
#include <random>
void reseal(std::string& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < bytes.size() - 8; ++i) { hash ^= static_cast<unsigned char>(bytes[i]); hash *= 1099511628211ULL; }
    for (unsigned i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = static_cast<char>((hash >> (i * 8)) & 255);
}
int main() {
    // Independently assembled LE v1 fixture: frozen count goal, one event, balance and nested containers.
    const std::string golden("\x44\x51\x53\x54\x41\x52\x43\x48\x01\x00\x00\x00\x69\x00\x00\x00\x44\x53\x48\x50\x41\x52\x43\x48\x01\x00\x00\x00\x4d\x00\x00\x00\x44\x45\x43\x4f\x41\x52\x43\x48\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\xf4\x01\x00\x00\x00\x00\x00\x00\x21\x00\x00\x00\x44\x49\x4e\x56\x41\x52\x43\x48\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x8e\x9f\xe1\xf0\x9f\xab\x3c\x3c\x00\x00\x00\x00\x44\x99\xa9\xe2\x28\x88\x0c\x46\x00\x00\x00\x00\x71\x26\x4e\x41\x75\x2d\x6d\xba\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x0a\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x64\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x0a\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xdb\x53\x9e\xee\x46\x81\x43\xe4", 250);
    player_api::PlayerSnapshot fixture;
    CHECK(player_api::decode_archive(golden, fixture) == player_api::ArchiveError::none);
    CHECK(fixture.quests.quests.size() == 1 && fixture.quests.quests[0].progress[0] == 2);
    CHECK(fixture.commerce.economy.balances.at(1) == 500 && fixture.quests.events[0].id == 1);
    std::string fixed;
    CHECK(player_api::encode_archive(fixture, fixed) == player_api::ArchiveError::none && static_cast<unsigned char>(fixed[8]) == 4);

    player_api::PlayerSnapshot migrated;
    CHECK(player_api::decode_archive(fixed, migrated) == player_api::ArchiveError::none && equal(fixture, migrated));
    CHECK(!migrated.quests.quests[0].definition.objectives[0].binding && !migrated.quests.events[0].fact);

    eco::Wallet legacy_wallet(eco::CurrencyCatalog({{1, 1000}}));
    auto legacy_assets = std::make_unique<eco::Economy>(std::move(legacy_wallet),
        std::vector<eco::Container>{{1, inv::Inventory(1, inv::ItemCatalog({{10, 10}}))}});
    auto current_definition = task(); current_definition.objectives[0].binding = quest::ObjectiveBinding{100, 10, quest::FactInput::quantity};
    player_api::Player legacy(quest::QuestCatalog({current_definition}), quest::default_objectives(), std::move(legacy_assets));
    CHECK(legacy.restore(fixture) == quest::Error::none);
    CHECK(!legacy.quests().find_accepted_quest(1)->definition.objectives[0].binding); // 不从当前配置补旧任务绑定。
    CHECK(legacy.observe_gameplay_fact({2, 100, 10, 1}).error == quest::Error::none && legacy.quests().find_accepted_quest(1)->progress[0] == 2);
    CHECK(legacy.apply_quest_progress_event({3, {{1, 10, 1}}}).error == quest::Error::none);
    CHECK(legacy.claim_quest_reward({99, 1, 1}).error == quest::Error::none && legacy.quests().find_accepted_quest(1)->fact_until == 0);

    auto p = player(); ready(p);
    CHECK(p.purchase_product(products(), {1, 100, 1, 1}).error == commerce::Error::none);
    CHECK(p.claim_quest_reward({2, 1, 1}).error == quest::Error::none);
    const auto original = p.snapshot();
    std::string bytes;
    CHECK(player_api::encode_archive(original, bytes) == player_api::ArchiveError::none);
    player_api::PlayerSnapshot decoded;
    CHECK(player_api::decode_archive(bytes, decoded) == player_api::ArchiveError::none && equal(original, decoded));
    std::string canonical;
    CHECK(player_api::encode_archive(decoded, canonical) == player_api::ArchiveError::none && canonical == bytes);
    const auto before = decoded;
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        CHECK(player_api::decode_archive(std::string_view(bytes).substr(0, length), decoded) != player_api::ArchiveError::none);
        CHECK(equal(before, decoded));
    }
    auto corrupt = bytes; corrupt[20] ^= 1;
    CHECK(player_api::decode_archive(corrupt, decoded) == player_api::ArchiveError::checksum_mismatch && equal(before, decoded));
    corrupt = bytes; corrupt[8] = 5;
    CHECK(player_api::decode_archive(corrupt, decoded) == player_api::ArchiveError::unsupported_version && equal(before, decoded));
    corrupt = bytes; corrupt.insert(corrupt.end() - 8, 'x'); reseal(corrupt);
    CHECK(player_api::decode_archive(corrupt, decoded) == player_api::ArchiveError::malformed && equal(before, decoded));
    corrupt = bytes; corrupt[corrupt.size() - 16] = 99; reseal(corrupt);
    CHECK(player_api::decode_archive(corrupt, decoded) == player_api::ArchiveError::malformed && equal(before, decoded));
    corrupt = std::string(player_api::archive_max_bytes + 1, 'x');
    CHECK(player_api::decode_archive(corrupt, decoded) == player_api::ArchiveError::too_large && equal(before, decoded));
    std::string keep = "keep";
    auto bad = original; bad.quests.claims[0].settlement.sequence = 99;
    CHECK(player_api::encode_archive(bad, keep) == player_api::ArchiveError::malformed && keep == "keep");
    bad = original; bad.quests.quests[0].claimed = false;
    CHECK(player_api::encode_archive(bad, keep) == player_api::ArchiveError::malformed && keep == "keep");
    bad = original; bad.quests.events.resize(quest::max_events + 1);
    CHECK(player_api::encode_archive(bad, keep) == player_api::ArchiveError::too_large && keep == "keep");
    auto restored = player(); restored.replace_quests(quest::QuestCatalog({}));
    CHECK(restored.restore(before) == quest::Error::none && restored.claim_quest_reward({2, 1, 1}).replayed);
    CHECK(restored.purchase_product(commerce::ProductCatalog({}), {1, 100, 1, 1}).replayed && equal(original, restored.snapshot()));
    bad = original; bad.quests.quests[0].definition.objectives[0].type = 99;
    CHECK(player_api::encode_archive(bad, keep) == player_api::ArchiveError::none); // Wire schema can carry a registered extension.
    CHECK(player_api::decode_archive(keep, decoded) == player_api::ArchiveError::none);
    CHECK(restored.restore(decoded) == quest::Error::invalid_snapshot && equal(original, restored.snapshot()));
    bad = original; bad.quests.events.clear();
    CHECK(player_api::encode_archive(bad, keep) == player_api::ArchiveError::none);
    CHECK(player_api::decode_archive(keep, decoded) == player_api::ArchiveError::none);
    CHECK(restored.restore(decoded) == quest::Error::invalid_snapshot && equal(original, restored.snapshot()));
    std::mt19937 random(20261018);
    for (int i = 0; i < 2000; ++i) {
        auto mutated = bytes;
        mutated[random() % mutated.size()] ^= static_cast<char>(1 + random() % 255);
        if (i % 2 == 0) reseal(mutated);
        const auto saved = decoded;
        const auto result = player_api::decode_archive(mutated, decoded);
        if (result != player_api::ArchiveError::none) CHECK(equal(saved, decoded));
        else { CHECK(player_api::encode_archive(decoded, canonical) == player_api::ArchiveError::none); CHECK(canonical == mutated); }
    }
    std::cout << "Player archive, invalid links, validation separation and 2000 mutations passed\n";
}
