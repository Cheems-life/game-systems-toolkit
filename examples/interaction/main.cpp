// 世界检查交互：本地业务成功产生可信事实，交互/任务一起提交；保存后用空目录恢复重放。
#include <designer/configuration/bundle.hpp>
#include <designer/player/archive.hpp>
#include <designer/persistence/file_store.hpp>
#include <iostream>
#include <stdexcept>
namespace q=designer::quests;
namespace p=designer::player;
namespace i=designer::interaction;
namespace c=designer::configuration;
namespace eco=designer::economy;
namespace inv=designer::inventory;
namespace storage=designer::persistence;
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
std::unique_ptr<p::Player> create(const c::Bundle& cfg,std::size_t events=256) {
    eco::Wallet wallet(cfg.currencies());
    auto assets=std::make_unique<eco::Economy>(std::move(wallet),std::vector<eco::Container>{{1,inv::Inventory(16,cfg.items())}});
    return std::make_unique<p::Player>(cfg.quests(),cfg.objectives(),std::move(assets),events,256);
}
std::string encode(const p::Player& player) {
    std::string bytes; require(p::encode_archive(player.snapshot(),bytes)==p::ArchiveError::none,"encode player"); return bytes;
}
int main(int argc,char** argv) {
    try {
        const bool load=argc==4 && std::string_view(argv[3])=="--load";
        if(argc!=3 && !load) {std::cerr<<"Usage: designer_interaction_example <game.json> <example-save> [--load]\nWithout --load replaces the specified example save.\n"; return 2;}
        const auto cfg=c::load(argv[1],q::default_objectives());
        if(cfg.error) {std::cerr<<cfg.error->source<<": "<<cfg.error->path<<": "<<cfg.error->message<<'\n'; return 1;}
        auto player=create(*cfg.bundle);
        const auto catalog = load ? i::Catalog({}) : cfg.bundle->interactions();
        if(load) {
            player->replace_quests(q::QuestCatalog({}));
            std::string bytes; require(storage::load_file(argv[2],bytes)==storage::FileError::none,"load file");
            p::PlayerSnapshot snapshot; require(p::decode_archive(bytes,snapshot)==p::ArchiveError::none,"decode v3");
            require(player->restore(snapshot)==q::Error::none,"restore interaction and fact links");
        } else {
            // 在小日志预算的玩家中演示任务投递失败：一次性交互不消耗，也不新增事实。
            auto small=create(*cfg.bundle,1);
            require(small->accept_quest(1,{5,0})==q::Error::none && small->accept_quest(2,{5,0})==q::Error::none,"accept quests");
            require(small->execute_interaction(catalog, {100,8}).error==i::Error::none,"repeatable inspection");
            const auto before=encode(*small); const auto failed=small->execute_interaction(catalog, {101,7});
            require(failed.error==i::Error::fact_rejected && failed.quest_error==q::Error::event_history_full,"expected task history limit");
            require(encode(*small)==before,"failed inspection must not consume point");
            // 以更大预算恢复同一状态，不丢弃原历史，然后用原请求 ID 重试。
            require(player->restore(small->snapshot())==q::Error::none,"restore with larger event budget");
            require(player->execute_interaction(catalog, {101,7}).error==i::Error::none,"retry one-time inspection");
            require(player->execute_interaction(catalog, {102,8}).error==i::Error::none,"repeatable new request");
            require(player->claim_quest_reward({1,1,1}).error==q::Error::none && player->claim_quest_reward({2,2,1}).error==q::Error::none,"claim completed quests");
            require(player->execute_interaction(catalog, {104,9}).error==i::Error::disabled,"disabled inspection");
        }
        const auto before=encode(*player);
        require(player->execute_interaction(catalog, {100,8}).replayed && player->execute_interaction(catalog, {101,7}).replayed && player->execute_interaction(catalog, {102,8}).replayed,"frozen result replay");
        require(player->execute_interaction(catalog, {103,7}).error==i::Error::already_completed,"changing request cannot repeat one-time point");
        require(player->execute_interaction(catalog, {100,7}).error==i::Error::request_conflict,"request payload conflict");
        require(player->claim_quest_reward({1,1,1}).replayed && encode(*player)==before,"replay changes no state");
        if(!load) require(storage::save_atomic(argv[2],before).error==storage::FileError::none,"save example");
        std::cout<<(load?"Restart restored":"Saved")<<": once/repeatable inspection, atomic quest delivery and request replay.\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
