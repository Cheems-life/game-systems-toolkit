// 任务集成测试辅助：构造通用目标、共享经济玩家并比较完整玩家快照，不包含具体玩法类型。
#pragma once
#include "../commerce/commerce_test_support.hpp"
#include <designer/player/archive.hpp>
namespace quest = designer::quests;
namespace player_api = designer::player;
inline quest::QuestDefinition task(quest::QuestId id = 1, eco::Amount reward = 100) {
    return {id, 1, 5, {{10, quest::counter_type, 1, 3}, {20, quest::flag_type, 1, 1}},
            {{{1, reward}}, {{10, 2}}}, quest::CompletionPolicy::all, "通用目标示例"};
}
inline player_api::Player player(eco::Amount reward = 100, std::size_t event_limit = 256,
                            std::size_t slots = 3, std::size_t economic_limit = 32) {
    return player_api::Player(products(), quest::QuestCatalog({task(1, reward), task(2, 10)}),
                         quest::default_objectives(), make_economy(economic_limit, slots).clone(), event_limit);
}
inline bool equal(const player_api::PlayerSnapshot& a, const player_api::PlayerSnapshot& b) {
    return equal(a.commerce, b.commerce) && a.quests == b.quests && a.interactions == b.interactions && a.dialogues == b.dialogues;
}
inline void ready(player_api::Player& player, quest::QuestId id = 1, quest::EventId event = 1) {
    CHECK(player.accept_quest(id, {5, event - 1}) == quest::Error::none);
    CHECK(player.apply_quest_progress_event({event, {{id, 10, 3}, {id, 20, 1}}}).error == quest::Error::none);
    CHECK(player.query_quest_status(id, {1, event}).status == quest::Status::ready);
}
