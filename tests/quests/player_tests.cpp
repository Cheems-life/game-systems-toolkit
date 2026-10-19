// 玩家集成测试：商城与任务使用同一钱包，验证一次性领奖、准备失败、请求冲突及统一恢复。
#include "quest_test_support.hpp"
int main() {
    auto p = player();
    ready(p);
    CHECK(p.purchase({42, 100, 2, 1}).error == commerce::Error::none);
    CHECK(p.economy().wallet().balance(1) == 300);
    const auto before = p.snapshot();
    CHECK(p.claim({42, 1, 1}).error == quest::Error::request_conflict && equal(before, p.snapshot()));
    const auto claim = p.claim({43, 1, 1});
    CHECK(claim.error == quest::Error::none && claim.receipt && !claim.replayed);
    CHECK(p.economy().wallet().balance(1) == 400 && total(*p.economy().container(1), 10) == 12);
    CHECK(p.query(1, {1, 1}).status == quest::Status::claimed);
    const auto committed = p.snapshot();
    CHECK(p.claim({43, 1, 1}).receipt == claim.receipt && p.claim({43, 1, 1}).replayed);
    CHECK(p.claim({44, 1, 1}).error == quest::Error::already_claimed && equal(committed, p.snapshot()));
    CHECK(p.claim({43, 2, 1}).error == quest::Error::request_conflict);
    CHECK(p.claim({43, 1, 2}).error == quest::Error::request_conflict);
    CHECK(p.claim({0, 1, 1}).error == quest::Error::invalid_request);
    CHECK(p.apply({2, {{1, 10, 1}}}).error == quest::Error::already_claimed && equal(committed, p.snapshot()));
    CHECK(p.purchase({43, 100, 1, 1}).error == commerce::Error::request_conflict);
    CHECK(p.claim({44, 2, 1}).error == quest::Error::not_accepted);
    CHECK(p.accept(2, {5, 1}) == quest::Error::none);
    CHECK(p.claim({44, 2, 1}).error == quest::Error::not_ready);

    auto funds = player(600); ready(funds);
    auto failed_before = funds.snapshot();
    auto failed = funds.claim({700, 1, 1});
    CHECK(failed.error == quest::Error::economy_failure && failed.economy_error == eco::Error::balance_limit_exceeded);
    CHECK(equal(failed_before, funds.snapshot()) && funds.query(1, {1, 1}).status == quest::Status::ready);
    CHECK(funds.purchase({701, 100, 1, 1}).error == commerce::Error::none);
    CHECK(funds.claim({700, 1, 1}).error == quest::Error::none && funds.economy().wallet().balance(1) == 1000);

    auto capacity_task = task(); capacity_task.reward.items = {{10, 11}};
    player_api::Player capacity(products(), quest::QuestCatalog({capacity_task}), quest::default_objectives(), make_economy(32, 1).clone());
    ready(capacity); failed_before = capacity.snapshot(); failed = capacity.claim({1, 1, 1});
    CHECK(failed.error == quest::Error::economy_failure && failed.inventory_error == inv::Error::insufficient_capacity);
    CHECK(equal(failed_before, capacity.snapshot()));
    CHECK(capacity.claim({1, 1, 2}).error == quest::Error::none); // A failed ID did not commit a destination or record.
    CHECK(total(*capacity.economy().container(2), 10) == 11);

    auto frozen = player(); CHECK(frozen.accept(1, {5, 0}) == quest::Error::none);
    auto newer = task(); newer.version = 2; newer.minimum_level = 99; newer.objectives[0].target = 100; newer.reward.currencies = {{1, 900}};
    frozen.replace_quests(quest::QuestCatalog({newer}));
    CHECK(frozen.apply({1, {{1, 10, 3}, {1, 20, 1}}}).error == quest::Error::none);
    frozen.replace_quests(quest::QuestCatalog({}));
    CHECK(frozen.claim({2, 1, 1}).error == quest::Error::none && frozen.economy().wallet().balance(1) == 600);
    auto restored = player(); restored.replace_products(commerce::ProductCatalog({})); restored.replace_quests(quest::QuestCatalog({}));
    CHECK(restored.restore(committed) == quest::Error::none && equal(committed, restored.snapshot()));
    CHECK(restored.claim({43, 1, 1}).replayed && restored.purchase({42, 100, 2, 1}).replayed);
    auto bad = committed; bad.commerce.economy.balances[1] = 1001;
    CHECK(restored.restore(bad) == quest::Error::invalid_snapshot && equal(committed, restored.snapshot()));
    bad = committed; bad.quests.claims[0].settlement.sequence = 99;
    CHECK(restored.restore(bad) == quest::Error::invalid_snapshot && equal(committed, restored.snapshot()));
    bad = committed; bad.quests.quests[0].progress[0] = 1;
    CHECK(restored.restore(bad) == quest::Error::invalid_snapshot && equal(committed, restored.snapshot()));
    bad = committed; bad.quests.claims.clear();
    CHECK(restored.restore(bad) == quest::Error::invalid_snapshot && equal(committed, restored.snapshot()));
    auto occupied = make_economy();
    CHECK(occupied.execute({8, 1, {}, task().reward}).error == eco::Error::none);
    player_api::Player collision(products(), quest::QuestCatalog({task()}), quest::default_objectives(), occupied.clone());
    ready(collision); const auto collision_before = collision.snapshot();
    CHECK(collision.claim({8, 1, 1}).error == quest::Error::request_conflict && equal(collision_before, collision.snapshot()));
    auto full = player(100, 256, 3, 1); ready(full);
    CHECK(full.purchase({1, 100, 1, 1}).error == commerce::Error::none);
    const auto full_before = full.snapshot(); failed = full.claim({2, 1, 1});
    CHECK(failed.economy_error == eco::Error::history_full && equal(full_before, full.snapshot()));
    std::cout << "Shared economy, one-time claims, frozen rules, failure safety and restoration passed\n";
}
