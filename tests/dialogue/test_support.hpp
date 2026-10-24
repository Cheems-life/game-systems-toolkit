#pragma once
// 对话集成支持：任务只绑定通用完成事实，不让任务核心依赖对话图。
#include "../quests/quest_test_support.hpp"
#include <designer/configuration/dialogue_content.hpp>
namespace d = designer::dialogue;
namespace cfg = designer::configuration;
inline d::Definition dialogue_definition(std::uint32_t version = 1)
{
    return {5001,
            version,
            1,
            400,
            {{1, d::NodeKind::text, 7, "npc.hello", 2},
             {2, d::NodeKind::choice, 7, "npc.question", 0, {{20, "answer.yes", 3}, {10, "answer.again", 1}}},
             {3, d::NodeKind::end}}};
}
inline d::Command start(d::RequestId request = 100)
{
    return {request, d::Action::start, 5001};
}
inline d::Command step(d::RequestId request, d::Action action, d::NodeId node, std::uint64_t revision,
                       d::OptionId option = 0, d::SessionId session = 100)
{
    return {request, action, 0, session, node, revision, option};
}
inline std::unique_ptr<player_api::Player> dialogue_player(std::size_t events = 256,
                                                           std::size_t history = 256)
{
    auto q = task();
    q.objectives = {{10, quest::flag_type, 1, 1, quest::ObjectiveBinding{400, 5001, quest::FactInput::one}}};
    return std::make_unique<player_api::Player>(quest::QuestCatalog({q}), quest::default_objectives(), make_economy().clone(), events,
        256, designer::interaction::Catalog({{7, 1, true, designer::interaction::Mode::repeatable, 400}}),
        256, d::Catalog({dialogue_definition()}), history);
}
inline void reach_end(player_api::Player &p)
{
    CHECK(p.execute_dialogue_command(start()).error == d::Error::none);
    CHECK(p.execute_dialogue_command(step(101, d::Action::advance, 1, 1)).error == d::Error::none);
    CHECK(p.execute_dialogue_command(step(102, d::Action::choose, 2, 2, 20)).error == d::Error::none);
}
