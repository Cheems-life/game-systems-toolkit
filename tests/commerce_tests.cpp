// 商城业务测试：检查商品配置、多份计价、数量溢出、购买失败以及改价/下架/移除后的旧请求重放。
// 同时检查经济副本独立性、请求冲突、恢复关联和 200 次模型对照购买，不涉及真实支付。

#include "commerce_test_support.hpp"
#include <limits>
#include <random>
#include <stdexcept>

int main() {
    const auto invalid_product = [](commerce::ProductDefinition definition) {
        bool rejected = false;
        try { commerce::ProductCatalog catalog({definition}); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    };
    auto definition = *products().find(100);
    auto invalid = definition; invalid.id = 0; invalid_product(invalid);
    invalid = definition; invalid.version = 0; invalid_product(invalid);
    invalid = definition; invalid.unit_cost.currencies.clear(); invalid_product(invalid);
    invalid = definition; invalid.unit_reward = {}; invalid_product(invalid);
    invalid = definition; invalid.unit_cost.currencies = {{1, 1}, {1, 2}}; invalid_product(invalid);
    invalid = definition; invalid.unit_reward.items = {{10, 0}}; invalid_product(invalid);
    invalid = definition; invalid.unit_reward.currencies = {{2, 0}}; invalid_product(invalid);
    invalid = definition; invalid.unit_reward.items.resize(65, {10, 1}); invalid_product(invalid);
    bool rejected = false;
    try { commerce::ProductCatalog duplicate({definition, definition}); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { commerce::ProductCatalog huge(std::vector<commerce::ProductDefinition>(commerce::max_products + 1)); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    commerce::ProductCatalog sorted({{1, 1, true, {{{2, 1}, {1, 1}}}, {{}, {{20, 1}, {10, 1}}}}});
    CHECK(sorted.find(1)->unit_cost.currencies[0].currency == 1 && sorted.find(1)->unit_reward.items[0].item == 10);
    CHECK(!sorted.find(99));
    rejected = false;
    try { commerce::Shop no_economy(products(), nullptr); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);

    auto shop = make_shop();
    const commerce::PurchaseRequest request{42, 100, 2, 1};
    auto result = shop.purchase(request);
    CHECK(result.error == commerce::Error::none && result.receipt && !result.replayed);
    CHECK(result.receipt->product_version == 1 && result.receipt->settlement.sequence == 1);
    CHECK(shop.economy().wallet().balance(1) == 300 && shop.economy().wallet().balance(2) == 92);
    CHECK(total(*shop.economy().container(1), 10) == 10);
    const auto first = shop.snapshot();
    const auto first_receipt = result.receipt;
    shop.replace_catalog(products(250, 2));
    result = shop.purchase(request);
    CHECK(result.error == commerce::Error::none && result.replayed && result.receipt == first_receipt);
    CHECK(equal(first, shop.snapshot()));
    CHECK(shop.purchase({43, 100, 1, 1}).error == commerce::Error::none);
    CHECK(shop.economy().wallet().balance(1) == 50 && total(*shop.economy().container(1), 10) == 15);
    failed(shop, {44, 100, 1, 1}, commerce::Error::economy_failure, eco::Error::insufficient_funds);
    failed(shop, {42, 100, 1, 1}, commerce::Error::request_conflict);
    failed(shop, {42, 200, 2, 1}, commerce::Error::request_conflict);
    failed(shop, {42, 100, 2, 2}, commerce::Error::request_conflict);
    failed(shop, {0, 100, 1, 1}, commerce::Error::invalid_request);
    failed(shop, {44, 100, 0, 1}, commerce::Error::invalid_request);
    failed(shop, {44, 0, 1, 1}, commerce::Error::invalid_request);
    shop.replace_catalog(products(250, 3, false));
    CHECK(shop.purchase(request).replayed);
    failed(shop, {45, 100, 1, 1}, commerce::Error::product_unavailable);
    shop.replace_catalog(commerce::ProductCatalog({}));
    CHECK(shop.purchase(request).receipt == first_receipt);
    failed(shop, {45, 100, 1, 1}, commerce::Error::unknown_product);

    auto capacity = make_shop(32, 32, 1);
    failed(capacity, {1, 100, 3, 1}, commerce::Error::economy_failure, eco::Error::inventory_failure, inv::Error::insufficient_capacity);
    failed(capacity, {1, 100, 1, 99}, commerce::Error::economy_failure, eco::Error::unknown_container);
    capacity.replace_catalog(products(1, 2));
    CHECK(capacity.purchase({1, 100, 1, 1}).error == commerce::Error::none); // Failed ID can retry.
    auto unknown_item = definition; unknown_item.unit_reward.items = {{99, 1}};
    capacity.replace_catalog(commerce::ProductCatalog({unknown_item}));
    failed(capacity, {2, 100, 1, 1}, commerce::Error::economy_failure, eco::Error::inventory_failure, inv::Error::unknown_item);
    auto retry = make_shop(); retry.replace_catalog(products(500, 1));
    failed(retry, {1, 100, 2, 1}, commerce::Error::economy_failure, eco::Error::insufficient_funds);
    retry.replace_catalog(products(100, 2));
    CHECK(retry.purchase({1, 100, 2, 1}).error == commerce::Error::none);
    auto currency_only = definition; currency_only.unit_reward.items.clear();
    commerce::Shop currency_shop(commerce::ProductCatalog({currency_only}), make_economy().clone());
    CHECK(currency_shop.purchase({1, 100, 2, 1}).error == commerce::Error::none);
    CHECK(currency_shop.economy().wallet().balance(1) == 300 && currency_shop.economy().wallet().balance(2) == 92);
    auto overflow = make_shop();
    auto expensive = definition; expensive.unit_cost.currencies = {{1, std::numeric_limits<eco::Amount>::max()}};
    overflow.replace_catalog(commerce::ProductCatalog({expensive}));
    failed(overflow, {1, 100, 2, 1}, commerce::Error::quantity_overflow);
    failed(overflow, {1, 100, 1, 1}, commerce::Error::economy_failure, eco::Error::insufficient_funds);
    expensive = definition; expensive.unit_reward.currencies = {{2, std::numeric_limits<eco::Amount>::max()}};
    overflow.replace_catalog(commerce::ProductCatalog({expensive}));
    failed(overflow, {1, 100, 2, 1}, commerce::Error::quantity_overflow);
    expensive = definition; expensive.unit_reward.items = {{10, std::numeric_limits<inv::Quantity>::max()}};
    overflow.replace_catalog(commerce::ProductCatalog({expensive}));
    failed(overflow, {1, 100, 2, 1}, commerce::Error::quantity_overflow);
    auto limited = make_shop(1);
    CHECK(limited.purchase(request).error == commerce::Error::none);
    failed(limited, {43, 100, 1, 1}, commerce::Error::history_full);
    CHECK(limited.purchase(request).replayed);
    auto ledger_limited = make_shop(5, 1);
    CHECK(ledger_limited.purchase(request).error == commerce::Error::none);
    failed(ledger_limited, {43, 100, 1, 1}, commerce::Error::economy_failure, eco::Error::history_full);
    CHECK(ledger_limited.purchase(request).replayed);

    auto base = make_economy();
    const eco::Transaction preexisting{42, 1, {{{1, 200}}}, {{{2, 2}}, {{10, 10}}}};
    CHECK(base.execute(preexisting).error == eco::Error::none);
    const auto base_snapshot = base.snapshot();
    auto clone = base.clone();
    CHECK(equal(base_snapshot, clone->snapshot()));
    CHECK(clone->execute({43, 1, {}, {{{1, 1}}, {}}}).error == eco::Error::none);
    CHECK(equal(base_snapshot, base.snapshot()));
    commerce::Shop collision(products(), base.clone());
    failed(collision, request, commerce::Error::request_conflict); // Exact economic replay is not a shop receipt.
    auto conflicting_request = request; conflicting_request.quantity = 1;
    failed(collision, conflicting_request, commerce::Error::request_conflict);
    // Existing non-shop economic records create valid sequence gaps.
    CHECK(collision.purchase({44, 100, 1, 1}).error == commerce::Error::none);
    CHECK(collision.snapshot().purchases[0].settlement.sequence == 2);
    auto restored = make_shop();
    restored.replace_catalog(commerce::ProductCatalog({}));
    CHECK(restored.restore(collision.snapshot()) == commerce::Error::none);
    CHECK(restored.purchase({44, 100, 1, 1}).replayed);
    const auto restored_before = restored.snapshot();
    auto bad = restored_before; bad.economy.balances[1] = 1001;
    CHECK(restored.restore(bad) == commerce::Error::invalid_snapshot && equal(restored_before, restored.snapshot()));
    bad = restored_before; bad.purchases[0].settlement.transaction.cost.currencies[0].amount += 1;
    CHECK(restored.restore(bad) == commerce::Error::invalid_snapshot && equal(restored_before, restored.snapshot()));
    bad = restored_before; bad.purchases[0].request.quantity = 3;
    CHECK(restored.restore(bad) == commerce::Error::invalid_snapshot && equal(restored_before, restored.snapshot()));
    bad = restored_before; bad.purchases[0].request.destination = 2;
    CHECK(restored.restore(bad) == commerce::Error::invalid_snapshot && equal(restored_before, restored.snapshot()));
    bad = restored_before; bad.purchases[0].product_version = 0;
    CHECK(restored.restore(bad) == commerce::Error::invalid_snapshot && equal(restored_before, restored.snapshot()));
    bad = restored_before; bad.economy.receipts.pop_back();
    CHECK(restored.restore(bad) == commerce::Error::invalid_snapshot && equal(restored_before, restored.snapshot()));

    auto two_orders = make_shop();
    CHECK(two_orders.purchase({1, 100, 1, 1}).error == commerce::Error::none);
    CHECK(two_orders.purchase({2, 100, 1, 1}).error == commerce::Error::none);
    auto one_order = make_shop(1); const auto one_before = one_order.snapshot();
    CHECK(one_order.restore(two_orders.snapshot()) == commerce::Error::invalid_snapshot && equal(one_before, one_order.snapshot()));
    auto simulation = make_shop(1024, 1024);
    std::mt19937 random(20261017);
    eco::Amount gold = 500, points = 90;
    std::uint64_t potions = 0, successes = 0;
    for (std::uint64_t id = 1; id <= 200; ++id) {
        const eco::Amount price = 1 + random() % 10;
        const commerce::Quantity quantity = 1 + random() % 5;
        simulation.replace_catalog(products(price, static_cast<std::uint32_t>(id)));
        const auto before = simulation.snapshot();
        const auto bought = simulation.purchase({id, 100, quantity, 1});
        const auto expected = price * quantity > gold ? eco::Error::insufficient_funds
            : quantity > 100 - points ? eco::Error::balance_limit_exceeded
            : potions + quantity * 5 > 30 ? eco::Error::inventory_failure : eco::Error::none;
        CHECK(bought.economy_error == expected);
        if (expected == eco::Error::none) {
            CHECK(bought.error == commerce::Error::none);
            gold -= price * quantity; points += quantity; potions += quantity * 5; ++successes;
            const auto committed = simulation.snapshot();
            simulation.replace_catalog(products(999, 999, false));
            CHECK(simulation.purchase({id, 100, quantity, 1}).receipt == bought.receipt);
            CHECK(equal(committed, simulation.snapshot()));
        } else CHECK(bought.error == commerce::Error::economy_failure && equal(before, simulation.snapshot()));
        CHECK(simulation.economy().wallet().balance(1) == gold && simulation.economy().wallet().balance(2) == points);
        CHECK(total(*simulation.economy().container(1), 10) == potions);
        CHECK(simulation.snapshot().purchases.size() == successes);
    }
    std::cout << "Products, scaling, replays across catalog changes, atomic purchase and restore passed\n";
}
