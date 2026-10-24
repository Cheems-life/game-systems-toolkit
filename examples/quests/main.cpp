// 示例：玩法适配层提交通用目标更新，商城和任务共享资产，保存后独立进程恢复并重放。
#include <designer/player/archive.hpp>
#include <designer/persistence/file_store.hpp>
#include <iostream>
#include <stdexcept>
namespace q = designer::quests;
namespace player_api = designer::player;
namespace eco = designer::economy;
namespace inv = designer::inventory;
namespace com = designer::commerce;
namespace storage = designer::persistence;
q::QuestDefinition definition() {
    return {1, 1, 5, {{10, q::counter_type, 1, 3}, {20, q::flag_type, 1, 1}},
            {{{1, 100}}, {{1001, 2}}}, q::CompletionPolicy::all, "准备出发"};
}
com::ProductCatalog example_products() {
    return com::ProductCatalog({{100, 1, true, {{{1, 100}}}, {{{2, 1}}, {{1001, 5}}}}});
}
player_api::Player create() {
    eco::Wallet wallet(eco::CurrencyCatalog({{1, 1000}, {2, 100}}));
    if (wallet.credit(1, 500) != eco::Error::none || wallet.credit(2, 90) != eco::Error::none)
        throw std::runtime_error("Invalid initial balances");
    auto economy = std::make_unique<eco::Economy>(std::move(wallet),
        std::vector<eco::Container>{{1, inv::Inventory(3, inv::ItemCatalog({{1001, 20}}))}});
    return player_api::Player(q::QuestCatalog({definition()}), q::default_objectives(), std::move(economy));
}
int main(int argc, char** argv) {
    const bool load = argc == 3 && std::string_view(argv[2]) == "--load";
    if (argc != 2 && !load) {
        std::cerr << "Usage: designer_quests_example <example-save-path> [--load]\n"
                     "Without --load the specified example file is replaced.\n";
        return 1;
    }
    auto player = create();
    const auto purchase_products = load ? com::ProductCatalog({}) : example_products();
    if (load) {
        player.replace_quests(q::QuestCatalog({}));
        std::string bytes; const auto loaded = storage::load_file(argv[1], bytes);
        if (loaded != storage::FileError::none) { std::cerr << storage::file_error_name(loaded) << '\n'; return 1; }
        player_api::PlayerSnapshot snapshot; const auto parsed = player_api::decode_archive(bytes, snapshot);
        if (parsed != player_api::ArchiveError::none) { std::cerr << player_api::archive_error_name(parsed) << '\n'; return 1; }
        if (player.restore(snapshot) != q::Error::none) return 1;
    } else {
        const auto unavailable = player.query_quest_status(1, {4, 100});
        if (unavailable.condition.reason != q::ConditionReason::level_too_low || player.accept_quest(1, {4, 100}) != q::Error::condition_denied) return 1;
        if (player.apply_quest_progress_event({100, {}}).error != q::Error::none || player.accept_quest(1, {5, 100}) != q::Error::none) return 1;
        // These adapters could be collecting evidence and finishing a dialogue; the core knows neither.
        if (player.apply_quest_progress_event({101, {{1, 10, 2}}}).error != q::Error::none) return 1;
        if (player.apply_quest_progress_event({102, {{1, 10, 1}, {1, 20, 1}}}).error != q::Error::none) return 1;
        if (player.query_quest_status(1, {1, 102}).status != q::Status::ready) return 1;
        auto updated = definition(); updated.version = 2; updated.objectives[0].target = 99; updated.reward.currencies[0].amount = 900;
        player.replace_quests(q::QuestCatalog({updated}));
        if (player.purchase_product(purchase_products, {42, 100, 1, 1}).error != com::Error::none || player.economy().wallet().balance(1) != 400) return 1;
        if (player.claim_quest_reward({43, 1, 1}).error != q::Error::none || player.economy().wallet().balance(1) != 500) return 1;
        std::cout << "Level gate, count + flag goals, frozen rewards and shared-wallet purchase/claim succeeded.\n";
    }
    std::string before, after;
    if (player_api::encode_archive(player.snapshot(), before) != player_api::ArchiveError::none) return 1;
    if (!player.apply_quest_progress_event({101, {{1, 10, 2}}}).replayed || !player.purchase_product(purchase_products, {42, 100, 1, 1}).replayed ||
        !player.claim_quest_reward({43, 1, 1}).replayed || player.claim_quest_reward({44, 1, 1}).error != q::Error::already_claimed) return 1;
    if (player_api::encode_archive(player.snapshot(), after) != player_api::ArchiveError::none || before != after) return 1;
    if (player.economy().wallet().balance(1) != 500 || player.query_quest_status(1, {0, 0}).status != q::Status::claimed) return 1;
    if (load) std::cout << "Restart: frozen quest and event/purchase/claim replays restored without current catalogs.\n";
    else {
        const auto saved = storage::save_atomic(argv[1], after);
        if (saved.error != storage::FileError::none) { std::cerr << storage::file_error_name(saved.error) << "; committed=" << saved.committed << '\n'; return 1; }
        std::cout << "Saved one player state containing assets, purchases, quests and histories.\n";
    }
}
