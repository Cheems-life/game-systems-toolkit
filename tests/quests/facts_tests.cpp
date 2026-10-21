// 事实路由核心测试：过滤、冻结、一对多、原文去重、时间边界、空匹配、批次失败与容量。
#include <designer/quests/quests.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace q = designer::quests;
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)
q::QuestDefinition task(q::QuestId id = 1) {
    return {id, 1, 1, {{10, q::counter_type, 1, 3, q::ObjectiveBinding{100, 1001, q::FactInput::quantity}},
                      {20, q::flag_type, 1, 1, q::ObjectiveBinding{200, 7, q::FactInput::one}}}, {{{1, 1}}, {}}};
}
int main() {
    q::QuestBook book(q::QuestCatalog({task(), task(2)}), q::default_objectives());
    CHECK(book.observe({1, 100, 1001, 2}).error == q::Error::none); // 未接任务也记录原文。
    CHECK(book.snapshot().events[0].updates.empty());
    CHECK(book.accept(1, {1, 1}) == q::Error::none);
    CHECK(book.observe({1, 100, 1001, 2}).replayed && book.accepted(1)->progress[0] == 0);
    CHECK(book.apply({1, {}}).error == q::Error::event_conflict); // 不同来源不能复用 ID。
    CHECK(book.observe({1, 100, 9999, 2}).error == q::Error::event_conflict);
    CHECK(book.observe({2, 999, 1001, 1}).error == q::Error::none);
    CHECK(book.observe({3, 100, 9999, 1}).error == q::Error::none);
    CHECK(book.accepted(1)->progress[0] == 0);
    CHECK(book.accept(2, {1, 3}) == q::Error::none);
    CHECK(book.observe({4, 100, 1001, 2}).error == q::Error::none);
    CHECK(book.accepted(1)->progress[0] == 2 && book.accepted(2)->progress[0] == 2);
    CHECK(book.snapshot().events.back().updates.size() == 2);
    CHECK(book.observe({5, 200, 7, 999}).error == q::Error::none); // one 不使用 quantity 数值。
    CHECK(book.accepted(1)->progress[1] == 1);
    const auto before = book.snapshot();
    CHECK(book.observe({5, 200, 7, 1}).error == q::Error::event_conflict && book.snapshot() == before);
    CHECK(book.observe({6, 100, 1001, std::numeric_limits<std::uint64_t>::max()}).error == q::Error::none);
    CHECK(book.query(1, {1, 6}).status == q::Status::ready);
    CHECK(book.observe({7, 100, 1001, 1}).error == q::Error::none); // ready 目标仍封顶。
    const auto ready = book.snapshot();
    for (const auto fact : std::vector<q::GameplayFact>{{0, 100, 1001, 1}, {8, 0, 1001, 1}, {8, 100, 0, 1}, {8, 100, 1001, 0}})
        CHECK(book.observe(fact).error == q::Error::invalid_update && book.snapshot() == ready);
    CHECK(book.observe({2, 999, 1001, 1}).replayed);
    CHECK(book.apply({8, {}}).error == q::Error::none);
    CHECK(book.observe({8, 900, 1, 1}).error == q::Error::event_conflict);
    CHECK(book.observe({1, 1, 1, 1}).error == q::Error::event_conflict);
    q::QuestBook late(q::QuestCatalog({task()}), q::default_objectives());
    CHECK(late.accept(1, {1, 10}) == q::Error::none);
    CHECK(late.observe({9, 100, 1001, 3}).error == q::Error::none && late.accepted(1)->progress[0] == 0);
    CHECK(late.observe({10, 100, 1001, 3}).error == q::Error::none && late.accepted(1)->progress[0] == 0);
    CHECK(late.observe({8, 100, 1001, 3}).error == q::Error::stale_event);
    CHECK(late.observe({11, 100, 1001, 1}).error == q::Error::none && late.accepted(1)->progress[0] == 1);
    auto changed = task(); changed.objectives[0].binding->subject = 9999;
    late.replace_catalog(q::QuestCatalog({changed}));
    CHECK(late.observe({12, 100, 1001, 1}).error == q::Error::none && late.accepted(1)->progress[0] == 2);
    CHECK(late.observe({13, 100, 9999, 1}).error == q::Error::none && late.accepted(1)->progress[0] == 2);
    q::QuestBook restored(q::QuestCatalog({}), q::default_objectives());
    CHECK(restored.restore(late.snapshot()) == q::Error::none && restored.snapshot() == late.snapshot());
    auto forged = late.snapshot(); forged.events.back().updates = {{1, 10, 1}};
    CHECK(restored.restore(forged) == q::Error::invalid_snapshot && restored.snapshot() == late.snapshot());
    forged = late.snapshot(); forged.events[2].updates.clear(); forged.quests[0].progress[0] = 1;
    CHECK(restored.restore(forged) == q::Error::invalid_snapshot); // 不能省掉应匹配目标并一起篡改进度。
    CHECK(late.apply({14, {{1, 10, 1}}, q::GameplayFact{14, 100, 9999, 1}}).error == q::Error::invalid_update);
    CHECK(late.apply({14, {}, q::GameplayFact{15, 100, 9999, 1}}).error == q::Error::invalid_update);
    q::QuestBook full(q::QuestCatalog({task()}), q::default_objectives(), 1);
    CHECK(full.observe({1, 1, 1, 1}).error == q::Error::none);
    CHECK(full.observe({2, 1, 1, 1}).error == q::Error::event_history_full);
    CHECK(full.observe({1, 1, 1, 1}).replayed);
    // 65 个匹配项不能截断为前 64 个；恰好 64 项则全部成功。
    std::vector<q::QuestDefinition> many;
    for (q::QuestId i = 1; i <= 65; ++i) { auto d = task(i); d.objectives.resize(1); many.push_back(d); }
    q::QuestBook overflow(q::QuestCatalog(many), q::default_objectives());
    for (q::QuestId i = 1; i <= 65; ++i) CHECK(overflow.accept(i, {1, 0}) == q::Error::none);
    const auto initial = overflow.snapshot();
    CHECK(overflow.observe({1, 100, 1001, 1}).error == q::Error::invalid_update && overflow.snapshot() == initial);
    many.pop_back(); q::QuestBook exact(q::QuestCatalog(many), q::default_objectives());
    for (q::QuestId i = 1; i <= 64; ++i) CHECK(exact.accept(i, {1, 0}) == q::Error::none);
    CHECK(exact.observe({1, 100, 1001, 1}).error == q::Error::none && exact.snapshot().events[0].updates.size() == 64);
    // 后项处理器拒绝（flag 使用 quantity=2），前项计数及事实记录都不能提交。
    auto mixed = task(); mixed.objectives[1].binding = q::ObjectiveBinding{100, 1001, q::FactInput::quantity};
    q::QuestBook atomic(q::QuestCatalog({mixed}), q::default_objectives()); CHECK(atomic.accept(1, {1, 0}) == q::Error::none);
    const auto zero = atomic.snapshot();
    CHECK(atomic.observe({1, 100, 1001, 2}).error == q::Error::invalid_update && atomic.snapshot() == zero);
    CHECK(atomic.observe({1, 100, 1001, 1}).error == q::Error::none); // 失败不占 ID。
    for (auto binding : std::vector<q::ObjectiveBinding>{{0, 1, q::FactInput::one}, {1, 0, q::FactInput::one}, {1, 1, static_cast<q::FactInput>(99)}}) {
        auto bad = task(); bad.objectives[0].binding = binding; bool threw = false;
        try { q::QuestCatalog invalid({bad}); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }
    std::cout << "Frozen fact routing, source identity, time boundaries and all-or-none fan-out passed\n";
}
