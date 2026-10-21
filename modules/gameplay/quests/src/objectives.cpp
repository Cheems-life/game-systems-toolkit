#include <utility>
#include <designer/quests/quests.hpp>
#include <algorithm>
#include <stdexcept>

namespace designer::quests {
ConditionResult evaluate(ConditionSpec spec, PlayerReadContext context) noexcept {
    if (spec.minimum_level == 0) return {false, ConditionReason::none, ConditionError::invalid_definition, spec.minimum_level, context.level};
    if (context.level == 0) return {false, ConditionReason::none, ConditionError::invalid_context, spec.minimum_level, context.level};
    if (context.level < spec.minimum_level) return {false, ConditionReason::level_too_low, ConditionError::none, spec.minimum_level, context.level};
    return {true, ConditionReason::none, ConditionError::none, spec.minimum_level, context.level};
}

namespace {
class Counter final : public ObjectiveHandler {         // 计数型处理器
public:
    bool validate(const ObjectiveDefinition& d, std::uint64_t p) const noexcept override {
        return d.rule_version == 1 && d.target > 0 && p <= d.target;
    }
    bool update(const ObjectiveDefinition& d, std::uint64_t p, std::uint64_t input, std::uint64_t& next) const noexcept override {
        if (!validate(d, p) || input == 0) return false;
        next = p + std::min(input, d.target - p);
        return true;
    }
    bool complete(const ObjectiveDefinition& d, std::uint64_t p) const noexcept override { return p == d.target; }
};

class Flag final : public ObjectiveHandler {            // 标记型处理器
public:
    bool validate(const ObjectiveDefinition& d, std::uint64_t p) const noexcept override {
        return d.rule_version == 1 && d.target == 1 && p <= 1;
    }
    bool update(const ObjectiveDefinition& d, std::uint64_t p, std::uint64_t input, std::uint64_t& next) const noexcept override {
        if (!validate(d, p) || input != 1) return false;
        next = 1;
        return true;
    }
    bool complete(const ObjectiveDefinition&, std::uint64_t p) const noexcept override { return p == 1; }
};

} // namespace

void ObjectiveRegistry::add(ObjectiveTypeId type, std::shared_ptr<const ObjectiveHandler> handler) {
    if (type == 0 || !handler || handlers_.size() >= 64 || !handlers_.emplace(type, std::move(handler)).second)
        throw std::invalid_argument("Invalid or duplicate objective handler");
}

const ObjectiveHandler* ObjectiveRegistry::find(ObjectiveTypeId type) const noexcept {
    const auto found = handlers_.find(type);
    return found == handlers_.end() ? nullptr : found->second.get();
}

ObjectiveRegistry default_objectives() {
    ObjectiveRegistry registry;
    registry.add(counter_type, std::make_shared<Counter>());
    registry.add(flag_type, std::make_shared<Flag>());
    return registry;
}

// 把一批任务定义整理成合法、顺序统一、可按 ID 查询的任务配置表
QuestCatalog::QuestCatalog(std::vector<QuestDefinition> definitions) {
    if (definitions.size() > max_quests) throw std::invalid_argument("Too many quests");
    for (auto& d : definitions) {
        // 检查配置是否合法
        if (d.id == 0 || d.version == 0 || d.minimum_level == 0 || d.objectives.empty() || d.objectives.size() > max_objectives ||
            d.policy != CompletionPolicy::all || d.title.size() > 128 ||
            (d.reward.currencies.empty() && d.reward.items.empty()) ||
            d.reward.currencies.size() > economy::max_entries_per_list || d.reward.items.size() > economy::max_entries_per_list)
            throw std::invalid_argument("Invalid quest definition");

        std::sort(d.objectives.begin(), d.objectives.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

        // 整理并检查目标列表
        ObjectiveId previous = 0;
        for (const auto& objective : d.objectives) {
            if (objective.id <= previous || objective.type == 0 || objective.rule_version == 0 || objective.target == 0 ||
                (objective.binding && !valid_binding(*objective.binding)))
                throw std::invalid_argument("Invalid objective definition");
            previous = objective.id;
        }

        // 整理并检查货币奖励
        std::sort(d.reward.currencies.begin(), d.reward.currencies.end(), [](const auto& a, const auto& b) { return a.currency < b.currency; });
        economy::CurrencyId currency = 0;
        for (const auto& value : d.reward.currencies) {
            if (value.currency <= currency || value.amount == 0) throw std::invalid_argument("Invalid quest currency reward");
            currency = value.currency;
        }
        // 整理并检查物品奖励
        std::sort(d.reward.items.begin(), d.reward.items.end(), [](const auto& a, const auto& b) { return a.item < b.item; });

        inventory::ItemId item = 0;
        for (const auto& value : d.reward.items) {
            if (value.item <= item || value.quantity == 0) throw std::invalid_argument("Invalid quest item reward");
            item = value.item;
        }
        const auto id = d.id;
        if (!definitions_.emplace(id, std::move(d)).second) throw std::invalid_argument("Duplicate quest ID");
    }
}

const QuestDefinition* QuestCatalog::find(QuestId id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}
} // namespace designer::quests
