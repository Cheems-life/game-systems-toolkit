// 配置业务集成测试：文件目录驱动共享资产购买/领奖；新内容无须改业务代码，坏包不影响现有玩家。
#include "test_support.hpp"
#include <designer/player/player.hpp>
namespace q = designer::quests;
namespace eco = designer::economy;
namespace inv = designer::inventory;
namespace c = designer::configuration;
int main() {
    const auto loaded = c::load(DESIGNER_CONFIG_FIXTURE, q::default_objectives()); CHECK(loaded.bundle);
    const auto& cfg = *loaded.bundle;
    eco::Wallet wallet(cfg.currencies()); CHECK(wallet.credit(1, 500) == eco::Error::none);
    auto economy = std::make_unique<eco::Economy>(std::move(wallet),
        std::vector<eco::Container>{{1, inv::Inventory(4, cfg.items())}});
    designer::player::Player player(cfg.products(), cfg.quests(), cfg.objectives(), std::move(economy));
    CHECK(player.purchase_product({1, 100, 1, 1}).error == designer::commerce::Error::none);
    CHECK(player.economy().wallet().balance(1) == 400 && player.economy().wallet().balance(2) == 1);
    CHECK(player.accept_quest(1, {4, 0}) == q::Error::condition_denied);
    CHECK(player.accept_quest(1, {5, 0}) == q::Error::none);
    CHECK(player.apply_quest_progress_event({1, {{1, 10, 3}, {1, 20, 1}}}).error == q::Error::none);
    CHECK(player.claim_quest_reward({2, 1, 1}).error == q::Error::none);
    CHECK(player.economy().wallet().balance(1) == 500);
    CHECK(player.economy().container(1)->slots()[0]->quantity == 7);
    // 同一程序使用额外的商品、物品与任务，均来自文件。
    CHECK(player.purchase_product({3, 200, 1, 1}).error == designer::commerce::Error::none);
    CHECK(player.accept_quest(2, {1, 1}) == q::Error::none);
    CHECK(player.apply_quest_progress_event({2, {{2, 1, 10}}}).error == q::Error::none);
    CHECK(player.claim_quest_reward({4, 2, 1}).error == q::Error::none);
    CHECK(player.economy().container(1)->slots()[1]->item_id == 1002);
    CHECK(player.economy().container(1)->slots()[1]->quantity == 3);
    const auto before = player.snapshot();
    auto broken = replace(fixture(), "\"item\": 1001", "\"item\": 9999");
    CHECK(!c::decode(broken, cfg.objectives()).bundle);
    CHECK(player.snapshot().commerce.economy.balances == before.commerce.economy.balances);
    CHECK(player.snapshot().quests == before.quests);
    CHECK(player.snapshot().commerce.purchases == before.commerce.purchases);
    CHECK(player.snapshot().commerce.economy.receipts == before.commerce.economy.receipts);
    CHECK(player.snapshot().commerce.economy.containers.size() == before.commerce.economy.containers.size());
    for (std::size_t i = 0; i < before.commerce.economy.containers.size(); ++i) {
        const auto now = player.snapshot().commerce.economy.containers[i];
        CHECK(now.id == before.commerce.economy.containers[i].id && now.slots == before.commerce.economy.containers[i].slots);
    }
    CHECK(player.purchase_product({1, 100, 1, 1}).replayed && player.claim_quest_reward({2, 1, 1}).replayed);
    // 加载新内容包不会自动热更玩家；显式接入后，新接任务用新规则，旧任务仍冻结。
    auto changed = replace(fixture(), "\"target\": 3", "\"target\": 30");
    const auto updated = c::decode(changed, cfg.objectives()); CHECK(updated.bundle);
    player.replace_quests(updated.bundle->quests());
    CHECK(player.quests().find_accepted_quest(1)->definition.objectives[0].target == 3);
    CHECK(updated.bundle->quests().find(1)->objectives[0].target == 30);
    std::cout << "File-driven purchases, quests, extra content and frozen history passed\n";
}
