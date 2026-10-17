// 任务核心测试：等级求值、处理器注册、通用多目标、冻结规则和事件批次，不链接商城协调器。
#include <designer/quests/quests.hpp>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace q = designer::quests;
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)
q::QuestDefinition task(q::QuestId id = 1) {
    return {id, 1, 5, {{10, q::counter_type, 1, 3}, {20, q::flag_type, 1, 1}}, {{{1, 100}}, {}}};
}
class DoubleCounter final : public q::ObjectiveHandler {
public:
    bool validate(const q::ObjectiveDefinition& d, std::uint64_t p) const noexcept override {
        return d.rule_version == 7 && d.target > 0 && d.target % 2 == 0 && p <= d.target && p % 2 == 0;
    }
    bool update(const q::ObjectiveDefinition& d, std::uint64_t p, std::uint64_t v, std::uint64_t& next) const noexcept override {
        if (!validate(d, p) || v == 0) return false;
        next = p + std::min(v, (d.target - p) / 2) * 2; return true;
    }
    bool complete(const q::ObjectiveDefinition& d, std::uint64_t p) const noexcept override { return p == d.target; }
};
int main() {
    auto condition = q::evaluate({5}, {4, 0});
    CHECK(!condition.allowed && condition.reason == q::ConditionReason::level_too_low && condition.error == q::ConditionError::none);
    CHECK(condition.required == 5 && condition.actual == 4);
    CHECK(q::evaluate({5}, {5, 0}).allowed && q::evaluate({5}, {6, 0}).allowed);
    CHECK(q::evaluate({5}, {0, 0}).error == q::ConditionError::invalid_context);
    CHECK(q::evaluate({0}, {5, 0}).error == q::ConditionError::invalid_definition);
    auto registry = q::default_objectives();
    bool rejected = false;
    try { registry.add(q::counter_type, std::make_shared<DoubleCounter>()); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    auto unknown = task(); unknown.objectives[0].type = 99;
    rejected = false;
    try { q::QuestBook book(q::QuestCatalog({unknown}), registry); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    unknown = task(); unknown.objectives[0].rule_version = 2;
    rejected = false;
    try { q::QuestBook book(q::QuestCatalog({unknown}), registry); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    q::QuestBook book(q::QuestCatalog({task(), task(2)}), registry);
    CHECK(book.query(1, {5, 0}).status == q::Status::available);
    CHECK(book.accept(1, {4, 10}) == q::Error::condition_denied);
    CHECK(book.accept(1, {5, 10}) == q::Error::none);
    CHECK(book.accept(1, {5, 10}) == q::Error::already_accepted);
    CHECK(book.accept(2, {5, 9}) == q::Error::invalid_context); // Acceptance boundaries cannot rewind either.
    CHECK(book.query(1, {1, 10}).status == q::Status::active); // No level gate after acceptance.
    const auto initial = book.snapshot();
    CHECK(book.apply({9, {{1, 10, 1}}}).error == q::Error::event_before_accept && book.snapshot() == initial);
    CHECK(book.apply({11, {{1, 10, 1}}}).error == q::Error::none);
    CHECK(book.apply({11, {{1, 10, 1}}}).replayed);
    const auto partial = book.snapshot();
    CHECK(book.apply({11, {{1, 10, 2}}}).error == q::Error::event_conflict && book.snapshot() == partial);
    CHECK(book.apply({10, {}}).error == q::Error::stale_event);
    CHECK(book.apply({12, {{1, 10, 1}, {1, 20, 2}}}).error == q::Error::invalid_update && book.snapshot() == partial);
    CHECK(book.apply({12, {{1, 10, 1}, {1, 10, 1}}}).error == q::Error::invalid_update);
    CHECK(book.apply({12, {{1, 10, 1}, {1, 99, 1}}}).error == q::Error::unknown_objective && book.snapshot() == partial);
    CHECK(book.apply({12, {{1, 20, 1}, {1, 10, std::numeric_limits<std::uint64_t>::max()}}}).error == q::Error::none);
    CHECK(book.apply({12, {{1, 10, std::numeric_limits<std::uint64_t>::max()}, {1, 20, 1}}}).replayed);
    CHECK(book.query(1, {0, 0}).status == q::Status::ready && book.accepted(1)->progress[0] == 3);
    CHECK(book.accept(2, {5, 11}) == q::Error::invalid_context);
    CHECK(book.accept(2, {5, 12}) == q::Error::none);
    const auto before_batch = book.snapshot();
    CHECK(book.apply({13, {{1, 20, 1}, {2, 99, 1}}}).error == q::Error::unknown_objective && book.snapshot() == before_batch);
    CHECK(book.apply({13, {{1, 20, 1}, {2, 10, 2}}}).error == q::Error::none);
    CHECK(book.apply({14, {}}).error == q::Error::none); // Record an observed fact even with no matching goals.
    auto changed = task(); changed.version = 2; changed.minimum_level = 99; changed.objectives[0].target = 100;
    book.replace_catalog(q::QuestCatalog({changed}));
    CHECK(book.accepted(1)->definition.version == 1 && book.accepted(1)->definition.objectives[0].target == 3);
    const auto snapshot = book.snapshot();
    q::QuestBook restored(q::QuestCatalog({}), q::default_objectives());
    CHECK(restored.restore(snapshot) == q::Error::none && restored.snapshot() == snapshot);
    auto bad = snapshot; bad.quests[0].progress[0] = 2;
    CHECK(restored.restore(bad) == q::Error::invalid_snapshot && restored.snapshot() == snapshot);
    bad = snapshot; bad.quests[0].definition.objectives[0].rule_version = 99;
    CHECK(restored.restore(bad) == q::Error::invalid_snapshot);
    bad = snapshot; bad.events.pop_back(); bad.events.pop_back();
    CHECK(restored.restore(bad) == q::Error::invalid_snapshot);
    bad = snapshot; bad.quests[0].claimed = true;
    CHECK(restored.restore(bad) == q::Error::invalid_snapshot && restored.snapshot() == snapshot);
    q::QuestBook limited(q::QuestCatalog({task()}), registry, 1);
    CHECK(limited.accept(1, {5, 0}) == q::Error::none);
    CHECK(limited.apply({1, {{1, 10, 1}}}).error == q::Error::none);
    CHECK(limited.apply({2, {{1, 20, 1}}}).error == q::Error::event_history_full);
    CHECK(limited.apply({1, {{1, 10, 1}}}).replayed);
    registry.add(77, std::make_shared<DoubleCounter>());
    auto custom = task(); custom.objectives = {{1, 77, 7, 4}};
    q::QuestBook extended(q::QuestCatalog({custom}), registry);
    CHECK(extended.accept(1, {5, 0}) == q::Error::none);
    CHECK(extended.apply({1, {{1, 1, 2}}}).error == q::Error::none);
    CHECK(extended.query(1, {1, 0}).status == q::Status::ready);
    CHECK(restored.restore(extended.snapshot()) == q::Error::invalid_snapshot); // Missing handler cannot silently drop a goal.
    std::cout << "Level conditions, registered handlers, frozen goals and atomic event boundaries passed\n";
}
