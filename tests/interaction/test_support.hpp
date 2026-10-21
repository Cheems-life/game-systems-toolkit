#pragma once
// 交互集成支持：共享资产与冻结绑定的示例玩家、完整状态比较。
#include "../quests/quest_test_support.hpp"
#include <designer/configuration/bundle.hpp>
namespace interact = designer::interaction;
inline interact::Catalog interaction_catalog() {
    return interact::Catalog({{7,1,true,interact::Mode::once,200},{8,1,true,interact::Mode::repeatable,300},{9,1,false,interact::Mode::once,200}});
}
inline quest::QuestDefinition interaction_task(quest::QuestId id=1) {
    auto d=task(id); d.objectives={{10,quest::counter_type,1,1,quest::ObjectiveBinding{300,8,quest::FactInput::one}},
                                  {20,quest::flag_type,1,1,quest::ObjectiveBinding{200,7,quest::FactInput::one}}}; return d;
}
inline std::unique_ptr<player_api::Player> interaction_player(std::size_t events=256,std::size_t history=256) {
    return std::make_unique<player_api::Player>(products(),quest::QuestCatalog({interaction_task()}),quest::default_objectives(),
        make_economy().clone(),events,256,interaction_catalog(),history);
}
