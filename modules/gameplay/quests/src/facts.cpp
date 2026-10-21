// 可信事实适配：精确过滤、映射输入、一对多；算法处理器继续只接收数字输入。
#include "fact_routing.hpp"
namespace designer::quests {
bool valid_fact(const GameplayFact& fact) noexcept {
    return fact.id != 0 && fact.type != 0 && fact.subject != 0 && fact.quantity != 0;
}
bool valid_binding(const ObjectiveBinding& binding) noexcept {
    return binding.fact_type != 0 && binding.subject != 0 &&
        (binding.input == FactInput::quantity || binding.input == FactInput::one);
}
namespace detail {
bool has_bindings(const QuestDefinition& definition) noexcept {
    for (const auto& objective : definition.objectives) if (objective.binding) return true;
    return false;
}
Error route_fact(const GameplayFact& fact, const std::map<QuestId, AcceptedQuest>& quests,
                 bool historical, std::vector<ObjectiveUpdate>& updates) {
    if (!valid_fact(fact)) return Error::invalid_update;
    std::vector<ObjectiveUpdate> candidate;
    for (const auto& entry : quests) {
        const auto& quest = entry.second;
        if (fact.id <= quest.accepted_after || (quest.claimed && (!historical || fact.id > quest.fact_until))) continue;
        for (const auto& objective : quest.definition.objectives) {
            if (!objective.binding) continue;
            const auto& binding = *objective.binding;
            if (binding.fact_type != fact.type || binding.subject != fact.subject) continue;
            if (candidate.size() == max_updates) return Error::invalid_update;
            candidate.push_back({entry.first, objective.id, binding.input == FactInput::one ? 1 : fact.quantity});
        }
    }
    updates.swap(candidate); return Error::none;
}
} // namespace detail
EventResult QuestBook::observe(const GameplayFact& fact) {
    if (!valid_fact(fact)) return {Error::invalid_update};
    const auto found = events_.find(fact.id);
    if (found != events_.end()) {
        if (!found->second.fact || !(*found->second.fact == fact)) return {Error::event_conflict};
        return {Error::none, true}; // 不能在接取、领奖或改表后重新匹配。
    }
    if (!events_.empty() && fact.id <= events_.rbegin()->first) return {Error::stale_event};
    if (events_.size() >= event_limit_) return {Error::event_history_full};
    ProgressEvent event{fact.id, {}, fact};
    const auto routed = detail::route_fact(fact, quests_, false, event.updates);
    if (routed != Error::none) return {routed};
    return apply(event);
}
} // namespace designer::quests
