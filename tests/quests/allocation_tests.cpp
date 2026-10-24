// 任务异常安全测试：逐点注入 bad_alloc，检查接取、进度、领奖、购买和存档不会半提交。
#include "quest_test_support.hpp"
#include <new>
namespace { long remaining = -1; }
void* operator new(std::size_t size) {
    if (remaining == 0) throw std::bad_alloc();
    if (remaining > 0) --remaining;
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
std::unique_ptr<player_api::Player> fresh() {
    return std::make_unique<player_api::Player>(quest::QuestCatalog({task(), task(2)}),
        quest::default_objectives(), make_economy().clone());
}
template<class Create, class Operation>
void sweep(Create create, Operation operation) {
    bool success = false;
    for (long budget = 0; budget < 3000; ++budget) {
        auto p = create(); const auto before = p->snapshot(); remaining = budget;
        try { const bool done = operation(*p); remaining = -1; CHECK(done); success = true; }
        catch (const std::bad_alloc&) { remaining = -1; CHECK(equal(before, p->snapshot())); }
        if (success) break;
    }
    CHECK(success);
}
int main() {
    const auto trusted_products = products();
    sweep(fresh, [](player_api::Player& p) { return p.accept_quest(1, {5, 0}) == quest::Error::none; });
    auto active = [] { auto p = fresh(); CHECK(p->accept_quest(1, {5, 0}) == quest::Error::none); return p; };
    sweep(active, [](player_api::Player& p) { return p.apply_quest_progress_event({1, {{1, 10, 3}, {1, 20, 1}}}).error == quest::Error::none; });
    auto prepared = [] { auto p = fresh(); ready(*p); CHECK(p->purchase_product(products(), {1, 100, 1, 1}).error == commerce::Error::none); return p; };
    // Includes failures after the candidate is marked claimed but before receipt/return allocations finish.
    sweep(prepared, [](player_api::Player& p) { return p.claim_quest_reward({2, 1, 1}).error == quest::Error::none; });
    sweep(prepared, [&trusted_products](player_api::Player& p) { return p.purchase_product(trusted_products, {3, 100, 1, 1}).error == commerce::Error::none; });
    auto source = prepared(); CHECK(source->claim_quest_reward({2, 1, 1}).error == quest::Error::none);
    const auto snapshot = source->snapshot();
    sweep(fresh, [&](player_api::Player& p) { return p.restore(snapshot) == quest::Error::none; });
    bool success = false;
    for (long budget = 0; budget < 3000; ++budget) {
        std::string output = "keep"; remaining = budget;
        try { const auto result = player_api::encode_archive(snapshot, output); remaining = -1; CHECK(result == player_api::ArchiveError::none); success = true; }
        catch (const std::bad_alloc&) { remaining = -1; CHECK(output == "keep"); }
        if (success) break;
    }
    CHECK(success);
    std::string bytes; CHECK(player_api::encode_archive(snapshot, bytes) == player_api::ArchiveError::none);
    success = false;
    for (long budget = 0; budget < 3000; ++budget) {
        player_api::PlayerSnapshot output; const auto before = output; remaining = budget;
        try { const auto result = player_api::decode_archive(bytes, output); remaining = -1; CHECK(result == player_api::ArchiveError::none && equal(snapshot, output)); success = true; }
        catch (const std::bad_alloc&) { remaining = -1; CHECK(equal(before, output)); }
        if (success) break;
    }
    CHECK(success);
    // 新路由/关闭边界在候选中分配失败时，也不能提前提交。
    auto bound = [] {
        auto definition = task();
        definition.objectives[0].binding = quest::ObjectiveBinding{100, 10, quest::FactInput::quantity};
        definition.objectives[1].binding = quest::ObjectiveBinding{200, 7, quest::FactInput::one};
        return std::make_unique<player_api::Player>(quest::QuestCatalog({definition}),
            quest::default_objectives(), make_economy().clone());
    };
    auto bound_active = [&] { auto p = bound(); CHECK(p->accept_quest(1, {5, 0}) == quest::Error::none); return p; };
    sweep(bound_active, [](player_api::Player& p) { return p.observe_gameplay_fact({1, 100, 10, 3}).error == quest::Error::none; });
    auto bound_ready = [&] {
        auto p = bound_active(); CHECK(p->observe_gameplay_fact({1, 100, 10, 3}).error == quest::Error::none);
        CHECK(p->observe_gameplay_fact({2, 200, 7, 1}).error == quest::Error::none); return p;
    };
    sweep(bound_ready, [](player_api::Player& p) { return p.claim_quest_reward({1, 1, 1}).error == quest::Error::none; });
    auto bound_source = bound_ready(); CHECK(bound_source->claim_quest_reward({1, 1, 1}).error == quest::Error::none);
    CHECK(bound_source->observe_gameplay_fact({3, 100, 10, 9}).error == quest::Error::none);
    const auto frozen = bound_source->snapshot();
    sweep(bound, [&](player_api::Player& p) { return p.restore(frozen) == quest::Error::none; });
    for (bool encoding : {true, false}) {
        std::string encoded; CHECK(player_api::encode_archive(frozen, encoded) == player_api::ArchiveError::none);
        bool done = false;
        for (long budget = 0; budget < 6000; ++budget) {
            std::string output = "keep"; player_api::PlayerSnapshot target; const auto old = target; remaining = budget;
            try {
                const auto error = encoding ? player_api::encode_archive(frozen, output) : player_api::decode_archive(encoded, target);
                remaining = -1; CHECK(error == player_api::ArchiveError::none);
                CHECK(encoding ? output == encoded : equal(target, frozen)); done = true;
            } catch (const std::bad_alloc&) { remaining = -1; CHECK(output == "keep" && equal(old, target)); }
            if (done) break;
        }
        CHECK(done);
    }
    std::cout << "Allocation failure sweeps preserve task, shared assets, records and codec outputs\n";
}
