#pragma once
#include <designer/player/player.hpp>
#include <algorithm>
#include <set>
namespace designer::player::detail
{
    // 对话自身重放由 Book 校验；这里仅验证跨系统完成事实及事件独占归属。
    inline bool valid_dialogue_links(const PlayerSnapshot &snapshot)
    {
        std::set<quests::EventId> owned;
        for (const auto &receipt : snapshot.interactions)
            if (!owned.insert(receipt.event_id).second)
                return false;
        for (const auto &receipt : snapshot.dialogues.commands)
        {
            if (!receipt.completion_event)
                continue;
            if (!owned.insert(receipt.completion_event).second)
                return false;
            const auto session =
                std::find_if(snapshot.dialogues.sessions.begin(), snapshot.dialogues.sessions.end(),
                             [&](const auto &s) { return s.id == receipt.session; });
            if (session == snapshot.dialogues.sessions.end())
                return false;
            const auto event =
                std::lower_bound(snapshot.quests.events.begin(), snapshot.quests.events.end(),
                                 receipt.completion_event, [](const auto &e, auto id) { return e.id < id; });
            const quests::GameplayFact expected{receipt.completion_event,
                                                session->definition.completion_fact_type,
                                                session->definition.id, 1};
            if (event == snapshot.quests.events.end() || event->id != receipt.completion_event ||
                !event->fact || !(*event->fact == expected))
                return false;
        }
        return true;
    }
} // namespace designer::player::detail
