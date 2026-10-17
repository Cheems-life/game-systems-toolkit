// 经济规则测试：检查钱包增减、金额上限、混合奖励、购买、请求去重和统一快照恢复。
// 包含 10000 次钱包操作及 500 次模型对照交易，检查失败原子性、溢出和重复请求不重复扣款发奖。

#include "economy_test_support.hpp"
#include <limits>
#include <random>
#include <stdexcept>

int main() {
    for (const auto& definitions : {std::vector<eco::CurrencyDefinition>{{0, 1}},
                                   std::vector<eco::CurrencyDefinition>{{1, 0}},
                                   std::vector<eco::CurrencyDefinition>{{1, 5}, {1, 10}}}) {
        bool rejected = false;
        try { eco::CurrencyCatalog rules(definitions); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
    const auto max = std::numeric_limits<eco::Amount>::max();
    eco::Wallet wallet(eco::CurrencyCatalog({{1, max}}));
    CHECK(wallet.balance(1) == 0 && wallet.balance(999) == 0);
    CHECK(wallet.credit(1, 0) == eco::Error::invalid_amount);
    CHECK(wallet.debit(1, 0) == eco::Error::invalid_amount);
    CHECK(wallet.credit(99, 1) == eco::Error::unknown_currency);
    CHECK(wallet.debit(99, 1) == eco::Error::unknown_currency);
    CHECK(wallet.credit(1, max) == eco::Error::none);
    const auto full = wallet.snapshot();
    CHECK(wallet.credit(1, 1) == eco::Error::balance_limit_exceeded && wallet.snapshot() == full);
    CHECK(wallet.restore({{99, 1}}) == eco::Error::unknown_currency && wallet.snapshot() == full);
    CHECK(wallet.restore({{1, 0}}) == eco::Error::invalid_amount && wallet.snapshot() == full);
    CHECK(wallet.debit(1, max) == eco::Error::none && wallet.snapshot().empty());
    CHECK(wallet.debit(1, 1) == eco::Error::insufficient_funds);
    CHECK(wallet.restore(full) == eco::Error::none && wallet.balance(1) == max);
    CHECK(wallet.restore({}) == eco::Error::none);
    eco::Wallet random_wallet(eco::CurrencyCatalog({{1, 1000}}));
    std::mt19937 random(20261017);
    eco::Amount expected = 0;
    for (int i = 0; i < 10000; ++i) {
        const eco::Amount amount = 1 + random() % 1200;
        if (random() % 2) {
            const bool success = amount <= 1000 - expected;
            CHECK(random_wallet.credit(1, amount) == (success ? eco::Error::none : eco::Error::balance_limit_exceeded));
            if (success) expected += amount;
        } else {
            const bool success = amount <= expected;
            CHECK(random_wallet.debit(1, amount) == (success ? eco::Error::none : eco::Error::insufficient_funds));
            if (success) expected -= amount;
        }
        CHECK(random_wallet.balance(1) == expected);
    }

    auto economy = make_economy();
    const auto first = economy.execute(purchase());
    CHECK(first.error == eco::Error::none && first.receipt && first.receipt->sequence == 1 && !first.replayed);
    CHECK(economy.wallet().balance(1) == 400 && economy.wallet().balance(2) == 95);
    CHECK(total(*economy.container(1), 10) == 5 && economy.history_size() == 1);
    CHECK(!economy.container(99));
    const auto committed = economy.snapshot();
    auto canonical = purchase(); canonical.reward.items = {{10, 5}};
    const auto replay = economy.execute(canonical);
    CHECK(replay.error == eco::Error::none && replay.replayed && replay.receipt == first.receipt);
    CHECK(equal(committed, economy.snapshot()));
    canonical.cost.currencies[0].amount = 101;
    failed(economy, canonical, eco::Error::request_conflict);
    auto request = purchase(2); request.cost.currencies[0].amount = 401;
    failed(economy, request, eco::Error::insufficient_funds);
    request = purchase(2); request.reward.currencies = {{2, 6}};
    failed(economy, request, eco::Error::balance_limit_exceeded);
    request = purchase(2); request.reward.items = {{10, 1}, {20, 3}};
    failed(economy, request, eco::Error::inventory_failure, inv::Error::insufficient_capacity);
    request.reward.items = {{10, 1}, {99, 1}};
    failed(economy, request, eco::Error::inventory_failure, inv::Error::unknown_item);
    request = purchase(2); request.destination = 99;
    failed(economy, request, eco::Error::unknown_container);
    request = purchase(0); failed(economy, request, eco::Error::invalid_request);
    request = purchase(2); request.cost.currencies = {{99, 1}};
    failed(economy, request, eco::Error::unknown_currency);
    request = purchase(2); request.reward.currencies = {{99, 1}};
    failed(economy, request, eco::Error::unknown_currency);
    request = purchase(2); request.reward.items = {{10, 0}};
    failed(economy, request, eco::Error::invalid_amount);
    request = purchase(2); request.cost.currencies = {{1, 0}};
    failed(economy, request, eco::Error::invalid_amount);
    request = purchase(2); request.reward.currencies = {{1, max}, {1, 1}};
    failed(economy, request, eco::Error::amount_overflow);
    request = purchase(2); request.reward.items = {{10, std::numeric_limits<inv::Quantity>::max()}, {10, 1}};
    failed(economy, request, eco::Error::amount_overflow);
    request = purchase(2); request.reward.items.resize(eco::max_entries_per_list + 1, {10, 1});
    failed(economy, request, eco::Error::invalid_request);
    request = {2, 1, {}, {}}; failed(economy, request, eco::Error::invalid_request);
    request = {2, 1, {{{1, 500}}}, {{{1, 500}}, {}}}; // Reward cannot fund its own cost.
    failed(economy, request, eco::Error::insufficient_funds);
    request = {2, 1, {{{1, 50}, {1, 50}}}, {{{1, 100}}, {}}};
    CHECK(economy.execute(request).error == eco::Error::none); // Cost first permits space under cap.
    CHECK(economy.wallet().balance(1) == 400 && economy.history_size() == 2);
    auto reused = purchase(3);
    CHECK(economy.execute(reused).error == eco::Error::none); // Failed IDs were never committed.

    auto limited = make_economy(1);
    const auto success = limited.execute(purchase());
    CHECK(success.error == eco::Error::none);
    failed(limited, purchase(2), eco::Error::history_full);
    CHECK(limited.execute(purchase()).replayed); // Full log still serves old requests.
    auto restored = make_economy();
    const auto snapshot = economy.snapshot();
    CHECK(restored.restore(snapshot) == eco::Error::none);
    CHECK(equal(restored.snapshot(), snapshot));
    const auto restored_before = restored.snapshot();
    CHECK(restored.execute(purchase()).replayed);
    CHECK(equal(restored_before, restored.snapshot()));
    auto invalid = snapshot; invalid.balances[1] = 1001;
    CHECK(restored.restore(invalid) == eco::Error::balance_limit_exceeded);
    CHECK(equal(restored_before, restored.snapshot()));
    invalid = snapshot; invalid.containers[1].slots[0] = inv::ItemStack{99, 1};
    CHECK(restored.restore(invalid) == eco::Error::invalid_snapshot);
    CHECK(equal(restored_before, restored.snapshot()));
    invalid = snapshot; invalid.receipts[0].sequence = 2;
    CHECK(restored.restore(invalid) == eco::Error::invalid_snapshot);
    invalid = snapshot; invalid.receipts[1].transaction.request_id = invalid.receipts[0].transaction.request_id;
    CHECK(restored.restore(invalid) == eco::Error::invalid_snapshot);
    invalid = snapshot; invalid.receipts[0].transaction.reward.items = {{10, 2}, {10, 3}};
    CHECK(restored.restore(invalid) == eco::Error::invalid_snapshot);
    invalid = snapshot; invalid.receipts[0].transaction.reward.items = {{99, 1}};
    CHECK(restored.restore(invalid) == eco::Error::invalid_snapshot);
    invalid = snapshot; invalid.receipts[0].transaction.reward.currencies = {{99, 1}};
    CHECK(restored.restore(invalid) == eco::Error::invalid_snapshot);
    invalid = snapshot; invalid.receipts[0].transaction.destination = 99;
    CHECK(restored.restore(invalid) == eco::Error::invalid_snapshot);
    CHECK(equal(restored_before, restored.snapshot()));
    CHECK(limited.restore(snapshot) == eco::Error::invalid_snapshot);
    const auto next = restored.execute({4, 1, {}, {{{1, 1}}, {}}});
    CHECK(next.error == eco::Error::none && next.receipt->sequence == 4);
    auto retries = make_economy();
    auto retry_request = purchase(80); retry_request.cost.currencies = {{1, 600}};
    failed(retries, retry_request, eco::Error::insufficient_funds);
    CHECK(retries.execute({81, 1, {}, {{{1, 100}}, {}}}).error == eco::Error::none);
    CHECK(retries.execute(retry_request).error == eco::Error::none); // Same failed request now succeeds.
    CHECK(retries.wallet().balance(1) == 0);
    auto cap = make_economy();
    CHECK(cap.execute({1, 1, {}, {{{1, 500}}, {}}}).error == eco::Error::none);
    CHECK(cap.wallet().balance(1) == 1000);
    CHECK(cap.execute({2, 1, {{{1, 100}}}, {{{1, 100}}, {}}}).error == eco::Error::none);
    CHECK(cap.wallet().balance(1) == 1000);
    failed(cap, {3, 1, {{{1, 1}, {2, 100}}}, {{}, {}}}, eco::Error::insufficient_funds);
    auto simulation = make_economy(1024);
    eco::Amount model_gold = 500;
    std::uint64_t model_items = 0, model_history = 0;
    for (eco::RequestId id = 1; id <= 500; ++id) {
        const eco::Amount cost = 1 + random() % 100;
        const eco::Amount reward = 1 + random() % 150;
        const inv::Quantity items = random() % 3 == 0 ? 1 + random() % 5 : 0;
        eco::Transaction generated{id, 1, {{{1, cost}}}, {{{1, reward}}, {}}};
        if (items) generated.reward.items.push_back({10, items});
        const auto before = simulation.snapshot();
        const auto result = simulation.execute(generated);
        const auto expected_error = cost > model_gold ? eco::Error::insufficient_funds
            : reward > 1000 - (model_gold - cost) ? eco::Error::balance_limit_exceeded
            : model_items + items > 30 ? eco::Error::inventory_failure : eco::Error::none;
        CHECK(result.error == expected_error);
        if (result.error == eco::Error::none) {
            model_gold = model_gold - cost + reward;
            model_items += items;
            ++model_history;
            const auto committed_state = simulation.snapshot();
            const auto repeated = simulation.execute(generated);
            CHECK(repeated.replayed && repeated.receipt == result.receipt);
            CHECK(equal(committed_state, simulation.snapshot()));
        } else CHECK(equal(before, simulation.snapshot()));
        CHECK(simulation.wallet().balance(1) == model_gold);
        CHECK(total(*simulation.container(1), 10) == model_items);
        CHECK(simulation.history_size() == model_history);
    }
    std::cout << "Wallet, rewards, purchases, canonical replays and whole-state restore passed\n";
}
