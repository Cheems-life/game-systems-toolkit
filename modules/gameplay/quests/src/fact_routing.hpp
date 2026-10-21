#pragma once
// 内部纯路由函数；历史模式使用冻结的领奖边界，不依赖当前目录或处理器。
#include <designer/quests/quests.hpp>
namespace designer::quests::detail {
[[nodiscard]] Error route_fact(const GameplayFact& fact, const std::map<QuestId, AcceptedQuest>& quests,
                               bool historical, std::vector<ObjectiveUpdate>& updates);
[[nodiscard]] bool has_bindings(const QuestDefinition& definition) noexcept;
}
