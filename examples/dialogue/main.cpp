// 分文件加载 → 对话/任务 → 显式发布新版 → 在选项节点存档 → 新进程空目录恢复 → 完成并领奖。
#include <designer/configuration/bundle.hpp>
#include <designer/configuration/dialogue_content.hpp>
#include <designer/player/archive.hpp>
#include <designer/persistence/file_store.hpp>
#include <iostream>
#include <stdexcept>
namespace cfg = designer::configuration;
namespace d = designer::dialogue;
namespace p = designer::player;
namespace q = designer::quests;
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void render(const p::Player &player, const cfg::TextTable &texts)
{
    const auto view = player.dialogues().query_dialogue_session(100);
    require(view.has_value(), "session missing");
    std::cout << "Dialogue " << view->definition.id << " v" << view->definition.version << ", node "
              << view->node.id << '\n';
    if (view->node.kind != d::NodeKind::end)
    {
        const auto *line = texts.find_text(view->node.text_key);
        require(line != nullptr, "missing displayed text");
        std::cout << *line << '\n';
        for (const auto &option : view->node.options)
        {
            const auto *label = texts.find_text(option.text_key);
            require(label != nullptr, "missing option text");
            std::cout << option.id << ": " << *label << '\n';
        }
    }
}
int main(int argc, char **argv)
{
    try
    {
        const bool load = argc == 4 && std::string_view(argv[3]) == "--load";
        if (argc != 3 && !load)
        {
            std::cerr << "Usage: designer_dialogue_example <content-directory> <example-save> "
                         "[--load]\nWithout --load replaces the specified example save.\n";
            return 2;
        }
        const std::filesystem::path directory = argv[1];
        const auto base = cfg::load(directory / "game.json", q::default_objectives());
        require(base.bundle != nullptr, "load base config");
        const auto language = cfg::load_dialogue_texts(directory / "zh-CN.json");
        require(language.content != nullptr, "load selected language");
        designer::economy::Wallet wallet(base.bundle->currencies());
        auto assets = std::make_unique<designer::economy::Economy>(
            std::move(wallet), std::vector<designer::economy::Container>{
                                   {1, designer::inventory::Inventory(16, base.bundle->items())}});
        p::Player player(base.bundle->products(), base.bundle->quests(), base.bundle->objectives(),
                         std::move(assets));
        if (load)
        {
            player.replace_quests(q::QuestCatalog({})); // 不需要当前任务/对话配置，恢复冻结定义。
            std::string bytes;
            require(designer::persistence::load_file(argv[2], bytes) ==
                        designer::persistence::FileError::none,
                    "read save");
            p::PlayerSnapshot snapshot;
            require(p::decode_archive(bytes, snapshot) == p::ArchiveError::none, "decode v4");
            require(player.restore(snapshot) == q::Error::none, "restore frozen dialogue");
            render(player, *language.content);
            const d::Command choose{102, d::Action::choose, 0, 100, 2, 2, 20};
            const d::Command finish{103, d::Action::finish, 0, 100, 3, 3};
            require(player.execute_dialogue_command(choose).error == d::Error::none, "choose answer");
            require(player.execute_dialogue_command(finish).error == d::Error::none, "finish and emit fact");
            require(player.query_quest_status(1, {5, 1}).status == q::Status::ready,
                    "quest completed by dialogue fact");
            require(player.claim_quest_reward({1, 1, 1}).error == q::Error::none, "claim quest reward");
            const auto before = player.snapshot();
            require(player.execute_dialogue_command(choose).replayed &&
                        player.execute_dialogue_command(finish).replayed,
                    "replay commands");
            require(before.dialogues == player.snapshot().dialogues &&
                        before.quests == player.snapshot().quests,
                    "replay leaves state unchanged");
            std::cout << "Restart restored frozen v1 flow; completion and reward succeeded once.\n";
        }
        else
        {
            const auto graph = cfg::load_dialogue_content(directory / "5001.json");
            require(graph.content != nullptr, "load selected dialogue");
            require(!cfg::validate_dialogue_text_references(graph.content->definition, *language.content),
                    "validate old text references");
            player.replace_dialogues(d::Catalog({graph.content->definition}));
            require(player.accept_quest(1, {5, 0}) == q::Error::none, "accept quest");
            require(player.execute_dialogue_command({100, d::Action::start, 5001}).error == d::Error::none,
                    "start dialogue");
            render(player, *language.content);
            const auto update = cfg::load_dialogue_content(directory / "5001-v2.json");
            require(update.content != nullptr, "load candidate v2");
            require(!cfg::validate_dialogue_text_references(update.content->definition, *language.content),
                    "validate new text references");
            player.replace_dialogues(d::Catalog({update.content->definition}));
            require(player.dialogues().query_dialogue_session(100)->definition.version == 1,
                    "active flow pins old version");
            require(player.execute_dialogue_command({101, d::Action::advance, 0, 100, 1, 1}).error ==
                        d::Error::none,
                    "advance to choice");
            render(player, *language.content);
            std::string bytes;
            require(p::encode_archive(player.snapshot(), bytes) == p::ArchiveError::none, "encode v4");
            require(designer::persistence::save_atomic(argv[2], bytes).error ==
                        designer::persistence::FileError::none,
                    "save example");
            std::cout << "Published v2 for future sessions; saved active v1 at choice node.\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
