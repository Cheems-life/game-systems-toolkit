// 玩法事实闭环：JSON 冻结绑定、一事实多目标、领奖后跳过、独立进程恢复及原文重放。
#include <designer/configuration/bundle.hpp>
#include <designer/player/archive.hpp>
#include <designer/persistence/file_store.hpp>
#include <iostream>
#include <stdexcept>
namespace q = designer::quests;
namespace p = designer::player;
namespace c = designer::configuration;
namespace eco = designer::economy;
namespace inv = designer::inventory;
namespace com = designer::commerce;
namespace storage = designer::persistence;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        const bool load = argc == 4 && std::string_view(argv[3]) == "--load";
        if (argc != 3 && !load) {
            std::cerr << "Usage: designer_quest_facts_example <bindings.json> <example-save> [--load]\n"
                         "Without --load, replaces only the specified example save file.\n"; return 2;
        }
        const auto result = c::load(argv[1], q::default_objectives());
        if (result.error) {
            std::cerr << result.error->source << ": " << result.error->path << ": " << result.error->message << '\n'; return 1;
        }
        const auto& cfg = *result.bundle;
        const auto purchase_products = load ? com::ProductCatalog({}) : cfg.products();
        eco::Wallet wallet(cfg.currencies()); require(wallet.credit(1, 500) == eco::Error::none, "initial balance");
        auto assets = std::make_unique<eco::Economy>(std::move(wallet),
            std::vector<eco::Container>{{1, inv::Inventory(16, cfg.items())}});
        p::Player player(cfg.quests(), cfg.objectives(), std::move(assets));
        // 事实类型/对象编号由本示例约定；事实中没有任何任务 ID 或目标 ID。
        const q::GameplayFact gained_first{1, 100, 1001, 3}, interacted{2, 200, 7, 9}, gained_again{3, 100, 1001, 7};
        if (load) {
            player.replace_quests(q::QuestCatalog({}));
            std::string bytes; require(storage::load_file(argv[2], bytes) == storage::FileError::none, "read save");
            p::PlayerSnapshot snapshot; require(p::decode_archive(bytes, snapshot) == p::ArchiveError::none, "decode save");
            require(player.restore(snapshot) == q::Error::none, "restore frozen bindings");
        } else {
            require(player.accept_quest(1, {5, 0}) == q::Error::none && player.accept_quest(2, {5, 0}) == q::Error::none, "accept tasks");
            require(player.observe_gameplay_fact(gained_first).error == q::Error::none, "one fact advances both tasks");
            require(player.snapshot().quests.events.back().updates.size() == 2, "two matched targets");
            require(player.observe_gameplay_fact(interacted).error == q::Error::none, "fixed one input");
            require(player.purchase_product(purchase_products, {10, 100, 1, 1}).error == com::Error::none, "shared purchase");
            require(player.claim_quest_reward({11, 1, 1}).error == q::Error::none, "first claim");
            player.replace_quests(q::QuestCatalog({})); // 已接任务的规则独立于当前目录。
            require(player.observe_gameplay_fact(gained_again).error == q::Error::none, "frozen second task");
            require(player.snapshot().quests.events.back().updates.size() == 1, "claimed task skipped");
            require(player.claim_quest_reward({12, 2, 1}).error == q::Error::none, "second claim");
            require(player.observe_gameplay_fact({4, 100, 1001, 1}).error == q::Error::none, "empty matches recorded");
        }
        std::string before, after;
        require(p::encode_archive(player.snapshot(), before) == p::ArchiveError::none, "encode before replay");
        require(player.observe_gameplay_fact(gained_first).replayed && player.observe_gameplay_fact(interacted).replayed && player.observe_gameplay_fact(gained_again).replayed, "fact replay");
        require(player.observe_gameplay_fact({2, 200, 7, 1}).error == q::Error::event_conflict, "same result but different raw quantity conflicts");
        require(player.claim_quest_reward({11, 1, 1}).replayed && player.purchase_product(purchase_products, {10, 100, 1, 1}).replayed, "asset replay");
        require(p::encode_archive(player.snapshot(), after) == p::ArchiveError::none && before == after, "replay changes nothing");
        if (!load) require(storage::save_atomic(argv[2], after).error == storage::FileError::none, "save example");
        std::cout << (load ? "Restart restored" : "Saved") << ": frozen bindings, multi-target facts, claim boundaries and raw fact replays.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
