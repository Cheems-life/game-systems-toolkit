// 事实存档与玩家集成：领奖前后路由重建、冻结绑定、原文重放及 v2 损坏/篡改拒绝。
#include "quest_test_support.hpp"
#include "fact_archive_golden.hpp"
#include <random>
namespace q = designer::quests;
q::QuestDefinition bound_task(q::QuestId id = 1) {
    auto d = task(id); d.objectives[0].binding = q::ObjectiveBinding{100, 10, q::FactInput::quantity};
    d.objectives[1].binding = q::ObjectiveBinding{200, 7, q::FactInput::one}; return d;
}
player_api::Player bound_player(std::size_t slots = 3) {
    return player_api::Player(q::QuestCatalog({bound_task(), bound_task(2)}),
                             q::default_objectives(), make_economy(32, slots).clone());
}
int main() {
    player_api::PlayerSnapshot golden_snapshot;
    CHECK(player_api::decode_archive(fact_golden, golden_snapshot) == player_api::ArchiveError::none);
    CHECK(golden_snapshot.quests.quests[0].definition.objectives[0].binding->subject == 10);
    CHECK(golden_snapshot.quests.events[0].fact->quantity == 2);
    std::string canonical_golden;
    CHECK(player_api::encode_archive(golden_snapshot, canonical_golden) == player_api::ArchiveError::none && canonical_golden == fact_golden_v4_empty);
    player_api::PlayerSnapshot v3; CHECK(player_api::decode_archive(fact_golden_v3_empty, v3) == player_api::ArchiveError::none && equal(v3, golden_snapshot));
    // 兼容样本的经济容器布局。
    eco::Wallet golden_wallet(eco::CurrencyCatalog({{1, 1000}}));
    auto golden_economy = std::make_unique<eco::Economy>(std::move(golden_wallet),
        std::vector<eco::Container>{{1, inv::Inventory(1, inv::ItemCatalog({{10, 10}}))}});
    player_api::Player golden_player(q::QuestCatalog({}), q::default_objectives(), std::move(golden_economy));
    CHECK(golden_player.restore(golden_snapshot) == q::Error::none);
    CHECK(golden_player.observe_gameplay_fact({1, 100, 10, 2}).replayed);
    CHECK(golden_player.observe_gameplay_fact({2, 100, 10, 1}).error == q::Error::none && golden_player.query_quest_status(1, {5, 2}).status == q::Status::ready);
    auto p = bound_player();
    CHECK(p.accept_quest(1, {5, 0}) == q::Error::none && p.accept_quest(2, {5, 0}) == q::Error::none);
    CHECK(p.observe_gameplay_fact({1, 100, 10, 3}).error == q::Error::none);
    CHECK(p.observe_gameplay_fact({2, 200, 7, 10}).error == q::Error::none);
    CHECK(p.claim_quest_reward({1, 1, 1}).error == q::Error::none);
    CHECK(p.quests().find_accepted_quest(1)->fact_until == 2);
    CHECK(p.observe_gameplay_fact({3, 100, 10, 1}).error == q::Error::none);
    CHECK(p.snapshot().quests.events.back().updates.size() == 1 && p.snapshot().quests.events.back().updates[0].quest == 2);
    CHECK(p.claim_quest_reward({2, 2, 1}).error == q::Error::none && p.quests().find_accepted_quest(2)->fact_until == 3);
    CHECK(p.observe_gameplay_fact({4, 100, 10, 100}).error == q::Error::none && p.snapshot().quests.events.back().updates.empty());
    p.replace_quests(q::QuestCatalog({}));
    CHECK(p.observe_gameplay_fact({1, 100, 10, 3}).replayed && p.observe_gameplay_fact({2, 200, 7, 10}).replayed);
    CHECK(p.observe_gameplay_fact({2, 200, 7, 1}).error == q::Error::event_conflict);
    const auto snapshot = p.snapshot();
    std::string bytes; CHECK(player_api::encode_archive(snapshot, bytes) == player_api::ArchiveError::none && bytes[8] == 4);
    player_api::PlayerSnapshot decoded; CHECK(player_api::decode_archive(bytes, decoded) == player_api::ArchiveError::none && equal(snapshot, decoded));
    auto restored = bound_player(); restored.replace_quests(q::QuestCatalog({}));
    CHECK(restored.restore(decoded) == q::Error::none && equal(snapshot, restored.snapshot()));
    CHECK(restored.observe_gameplay_fact({1, 100, 10, 3}).replayed && restored.observe_gameplay_fact({4, 100, 10, 100}).replayed);
    CHECK(restored.claim_quest_reward({1, 1, 1}).replayed && restored.claim_quest_reward({3, 1, 1}).error == q::Error::already_claimed);
    const auto unchanged = restored.snapshot();
    auto reject = [&](player_api::PlayerSnapshot bad) {
        CHECK(restored.restore(bad) == q::Error::invalid_snapshot && equal(unchanged, restored.snapshot()));
    };
    auto bad = snapshot; bad.quests.quests[0].fact_until = 0; reject(bad);
    bad = snapshot; bad.quests.quests[0].fact_until = 4; reject(bad); // 关闭太晚会遗漏本应匹配的事件 3/4。
    bad = snapshot; bad.quests.quests[0].fact_until = 1; reject(bad); // 关闭前还未完成。
    bad = snapshot; bad.quests.quests[0].fact_until = 99; reject(bad);
    bad = snapshot; bad.quests.events[0].fact->subject = 99; reject(bad);
    bad = snapshot; bad.quests.events[0].updates.pop_back(); bad.quests.quests[1].progress[0] = 0; reject(bad);
    bad = snapshot; bad.quests.quests[0].definition.objectives[0].binding->subject = 99; reject(bad);
    bad = snapshot; bad.quests.events.back().updates = {{1, 10, 100}}; reject(bad);
    bad = snapshot; bad.quests.events[0].fact->id = 99;
    std::string keep = "keep"; CHECK(player_api::encode_archive(bad, keep) == player_api::ArchiveError::malformed && keep == "keep");
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CHECK(player_api::decode_archive(std::string_view(bytes).substr(0, n), decoded) != player_api::ArchiveError::none);
        CHECK(equal(snapshot, decoded));
    }
    const auto seal = [](std::string& data) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (std::size_t i = 0; i < data.size() - 8; ++i) { hash ^= static_cast<unsigned char>(data[i]); hash *= 1099511628211ULL; }
        for (unsigned i = 0; i < 8; ++i) data[data.size() - 8 + i] = static_cast<char>((hash >> (i * 8)) & 255);
    };
    // 偏移来自独立 v2 样本布局，分别破坏 binding / fact 标记和枚举。
    for (const auto offset : {std::size_t{194}, std::size_t{259}, std::size_t{203}}) {
        auto corrupt = fact_golden; corrupt[offset] = static_cast<char>(offset == 203 ? 99 : 2); seal(corrupt);
        CHECK(player_api::decode_archive(corrupt, decoded) == player_api::ArchiveError::malformed && equal(decoded, snapshot));
    }
    std::mt19937 random(20261021);
    for (int i = 0; i < 2000; ++i) {
        auto corrupt = bytes; corrupt[random() % corrupt.size()] ^= static_cast<char>(1 + random() % 255);
        if (i % 2 == 0) seal(corrupt);
        const auto previous = decoded;
        const auto result = player_api::decode_archive(corrupt, decoded);
        if (result != player_api::ArchiveError::none) CHECK(equal(previous, decoded));
        else {
            std::string canonical; CHECK(player_api::encode_archive(decoded, canonical) == player_api::ArchiveError::none);
            if (static_cast<unsigned char>(corrupt[8]) == 4) CHECK(canonical == corrupt);
        }
    }
    // 发奖失败不能提前关掉事实匹配；后续仍能接收事实。
    auto full = bound_player(1); CHECK(full.accept_quest(1, {5, 0}) == q::Error::none);
    CHECK(full.observe_gameplay_fact({1, 100, 10, 3}).error == q::Error::none && full.observe_gameplay_fact({2, 200, 7, 1}).error == q::Error::none);
    CHECK(full.purchase_product(products(), {10, 100, 1, 1}).error == commerce::Error::none); // 背包占用；任务奖励与已有同物品可能合并。
    auto filled = full.snapshot(); filled.commerce.economy.containers[0].slots[0] = inv::ItemStack{10, 10};
    CHECK(full.restore(filled) == q::Error::none);
    const auto prior = full.snapshot(); CHECK(full.claim_quest_reward({11, 1, 1}).error == q::Error::economy_failure);
    CHECK(equal(prior, full.snapshot()) && full.quests().find_accepted_quest(1)->fact_until == 0);
    CHECK(full.observe_gameplay_fact({3, 100, 10, 1}).error == q::Error::none && full.snapshot().quests.events.back().updates.size() == 1);
    std::cout << "Fact archive and claim boundary reconstruction passed\n";
}
