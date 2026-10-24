#include "application.hpp"
#include <designer/player/archive.hpp>
#include <designer/persistence/file_store.hpp>
#include <algorithm>
#include <charconv>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace designer::app {
namespace {
std::uint32_t number(const std::string& value) {
    std::uint32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0)
        throw std::invalid_argument("参数必须是正整数");
    return result;
}
void arguments(const std::vector<std::string>& words, std::size_t size) {
    if (words.size() != size) throw std::invalid_argument("参数数量错误，输入 help 查看用法");
}
std::string diagnostic(const configuration::Diagnostic& error) {
    return error.source + " " + error.path + ": " + error.message;
}
const char* status_name(quests::Status status) {
    switch (status) {
    case quests::Status::locked: return "未解锁";
    case quests::Status::available: return "可接取";
    case quests::Status::active: return "进行中";
    case quests::Status::ready: return "可领奖";
    case quests::Status::claimed: return "已领奖";
    }
    return "未知";
}
const char* item_name(inventory::ItemId id) {
    // Display aliases for this small reference game, not domain rules.
    if (id == 1001) return "药水";
    if (id == 1002) return "补给材料";
    return "物品";
}
void help() {
    std::cout << "\n新手村命令（英文输入，中文反馈）：\n"
        "  status             玩家资产和背包\n"
        "  shop               商品与价格\n"
        "  buy <商品ID> <份数> 例如 buy 100 1\n"
        "  quests             任务列表和目标进度\n"
        "  accept <任务ID>     例如 accept 1\n"
        "  inspect <交互点ID>  例如 inspect 7，调查路标\n"
        "  talk <对话ID>       例如 talk 5001，找向导\n"
        "  dialogue           查看当前对话和选项\n"
        "  next               推进文本节点\n"
        "  choose <选项ID>     例如 choose 20\n"
        "  finish             确认完成对话，推进任务\n"
        "  cancel             取消当前对话，不产生完成事实\n"
        "  claim <任务ID>      例如 claim 1\n"
        "  save / load        保存 / 重新加载指定存档\n"
        "  help / quit        帮助 / 退出（不自动保存）\n"
        "建议：shop → buy 100 1 → accept 1 → inspect 7 → talk 5001\n"
        "      → next → choose 20 → finish → quests → claim 1 → save\n";
}
} // namespace

Application::Application(const std::filesystem::path& directory, std::filesystem::path save)
    : dialogue_catalog_({}), save_path_(std::move(save)) {
    auto loaded = configuration::load(directory / "game.json", quests::default_objectives());
    if (loaded.error) throw std::runtime_error(diagnostic(*loaded.error));
    content_ = std::move(loaded.bundle);
    auto graph = configuration::load_dialogue_content(directory / "5001.json");
    if (graph.error) throw std::runtime_error(diagnostic(*graph.error));
    auto language = configuration::load_dialogue_texts(directory / "zh-CN.json");
    if (language.error) throw std::runtime_error(diagnostic(*language.error));
    if (const auto error = configuration::validate_dialogue_text_references(graph.content->definition, *language.content))
        throw std::runtime_error(diagnostic(*error));
    texts_ = std::move(language.content);
    dialogue_catalog_ = dialogue::Catalog({graph.content->definition});
    economy::Wallet wallet(content_->currencies());
    if (wallet.credit(1, 500) != economy::Error::none)
        throw std::runtime_error("启动配置无法容纳初始金币");
    auto assets = std::make_unique<economy::Economy>(std::move(wallet),
        std::vector<economy::Container>{{1, inventory::Inventory(16, content_->items())}});
    player_ = std::make_unique<player::Player>(content_->quests(), content_->objectives(), std::move(assets),
        256, 256, 256);
    std::cout << "=== Designer 新手村：应用层体验 ===\n存档：" << save_path_.string() << '\n';
    // Never silently replace an unreadable/corrupted existing save with a new player.
    if (std::filesystem::exists(save_path_)) {
        if (!load_game()) throw std::runtime_error("已有存档恢复失败，文件保持原样");
    } else {
        dirty_ = true;
        std::cout << "新玩家：等级5，初始500金币，背包16格。\n";
    }
}

std::uint64_t Application::request_id() {
    if (next_request_ == 0) throw std::runtime_error("请求ID已耗尽");
    const auto result = next_request_;
    next_request_ = result == std::numeric_limits<std::uint64_t>::max() ? 0 : result + 1;
    return result;
}
void Application::reset_request_cursor(const player::PlayerSnapshot& state) {
    std::uint64_t largest = 0;
    for (const auto& row : state.commerce.economy.receipts)
        largest = std::max(largest, row.transaction.request_id);
    for (const auto& row : state.interactions) largest = std::max(largest, row.request.request_id);
    for (const auto& row : state.dialogues.commands) largest = std::max(largest, row.command.request_id);
    for (const auto& session : state.dialogues.sessions) largest = std::max(largest, session.id);
    next_request_ = largest == std::numeric_limits<std::uint64_t>::max() ? 0 : largest + 1;
}

void Application::show_status() const {
    const auto& assets = player_->economy();
    std::cout << "\n等级：5\n金币：" << assets.wallet().balance(1)
              << "\n积分：" << assets.wallet().balance(2) << '\n';
    const auto* bag = assets.container(1);
    for (std::size_t i = 0; i < bag->slots().size(); ++i) {
        const auto& slot = bag->slots()[i];
        if (slot) std::cout << "格子" << i + 1 << "：" << item_name(slot->item_id)
                            << " [" << slot->item_id << "] × " << slot->quantity << '\n';
    }
    std::cout << (dirty_ ? "有未保存进度\n" : "当前进度已保存\n");
}
void Application::show_shop() const {
    for (const auto& entry : content_->products().entries()) {
        const auto& product = entry.second;
        std::cout << "商品 " << product.id << (product.available ? " [上架]" : " [下架]") << " 每份消耗：";
        for (const auto& cost : product.unit_cost.currencies)
            std::cout << "货币" << cost.currency << "×" << cost.amount << ' ';
        std::cout << "；奖励：";
        for (const auto& reward : product.unit_reward.items)
            std::cout << item_name(reward.item) << "×" << reward.quantity << ' ';
        for (const auto& reward : product.unit_reward.currencies)
            std::cout << "货币" << reward.currency << "×" << reward.amount << ' ';
        std::cout << '\n';
    }
}
void Application::show_quests() const {
    const auto cursor = player_->quests().event_cursor();
    for (const auto& row : content_->quests().entries()) {
        const auto view = player_->query_quest_status(row.first, {5, cursor});
        std::cout << "任务 " << row.first << "：" << row.second.title << " [" << status_name(view.status) << "]\n";
        if (const auto* accepted = player_->quests().find_accepted_quest(row.first)) {
            for (std::size_t i = 0; i < accepted->progress.size(); ++i) {
                const auto& goal = accepted->definition.objectives[i];
                std::cout << "  目标" << goal.id << "：" << accepted->progress[i] << '/' << goal.target;
                if (goal.binding) std::cout << (goal.binding->subject == 7 ? " 调查路标" : " 完成向导对话");
                std::cout << '\n';
            }
        }
    }
}
std::optional<dialogue::SessionId> Application::active_session() const {
    const auto state = player_->dialogues().snapshot();
    for (const auto& session : state.sessions)
        if (session.status == dialogue::Status::active) return session.id;
    return std::nullopt;
}
void Application::show_dialogue() const {
    const auto session = active_session();
    if (!session) { std::cout << "没有进行中的对话，用 talk 5001 开始。\n"; return; }
    const auto view = player_->dialogues().query_dialogue_session(*session);
    const auto text = [this](const std::string& key) {
        const auto* line = texts_->find_text(key);
        return line ? *line : "[缺少文本：" + key + "]";
    };
    if (view->node.kind == dialogue::NodeKind::end) {
        std::cout << "对话已到结束节点，输入 finish 确认完成。\n"; return;
    }
    std::cout << "向导：" << text(view->node.text_key) << '\n';
    if (view->node.kind == dialogue::NodeKind::choice) {
        for (const auto& option : view->node.options)
            std::cout << "  " << option.id << "：" << text(option.text_key) << '\n';
        std::cout << "输入 choose <选项ID>。\n";
    } else std::cout << "输入 next 继续，或 save 保存中途进度。\n";
}

bool Application::save_game() {
    std::string bytes;
    const auto encoded = player::encode_archive(player_->snapshot(), bytes);
    if (encoded != player::ArchiveError::none) {
        std::cout << "保存失败：" << player::archive_error_name(encoded) << '\n'; return false;
    }
    if (save_path_.has_parent_path()) std::filesystem::create_directories(save_path_.parent_path());
    const auto saved = persistence::save_atomic(save_path_, bytes);
    if (saved.error != persistence::FileError::none) {
        std::cout << "保存失败：" << persistence::file_error_name(saved.error)
                  << (saved.committed ? "（文件已替换，但持久性未确认）" : "（未替换正式文件）") << '\n';
        dirty_ = true; return false;
    }
    dirty_ = false;
    std::cout << "保存成功：" << save_path_.string() << '\n'; return true;
}
bool Application::load_game() {
    std::string bytes;
    const auto loaded = persistence::load_file(save_path_, bytes);
    if (loaded != persistence::FileError::none) {
        std::cout << "读取失败：" << persistence::file_error_name(loaded) << '\n'; return false;
    }
    player::PlayerSnapshot state;
    const auto decoded = player::decode_archive(bytes, state);
    if (decoded != player::ArchiveError::none) {
        std::cout << "解码失败：" << player::archive_error_name(decoded) << '\n'; return false;
    }
    const auto restored = player_->restore(state);
    if (restored != quests::Error::none) {
        std::cout << "恢复失败：" << quests::error_name(restored) << '\n'; return false;
    }
    reset_request_cursor(state); dirty_ = false;
    std::cout << "恢复成功：资产、任务、交互和对话会话。\n";
    return true;
}

bool Application::dispatch(const std::vector<std::string>& w) {
    const auto& command = w[0];
    if (command == "help") { arguments(w, 1); help(); }
    else if (command == "status") { arguments(w, 1); show_status(); }
    else if (command == "shop") { arguments(w, 1); show_shop(); }
    else if (command == "quests") { arguments(w, 1); show_quests(); }
    else if (command == "dialogue") { arguments(w, 1); show_dialogue(); }
    else if (command == "save") { arguments(w, 1); return save_game(); }
    else if (command == "load") { arguments(w, 1); return load_game(); }
    else if (command == "quit") { arguments(w, 1); quit_ = true; }
    else if (command == "buy") {
        arguments(w, 3);
        const auto id = number(w[1]), count = number(w[2]);
        const auto result = player_->purchase_product(content_->products(), {request_id(), id, count, 1});
        if (result.error != commerce::Error::none) {
            std::cout << "购买失败：" << commerce::error_name(result.error) << " / "
                      << economy::error_name(result.economy_error) << " / "
                      << inventory::error_name(result.inventory_error) << '\n'; return false;
        }
        dirty_ = true; std::cout << "购买成功。\n"; show_status();
    } else if (command == "accept") {
        arguments(w, 2);
        const auto result = player_->accept_quest(number(w[1]), {5, player_->quests().event_cursor()});
        if (result != quests::Error::none) {
            std::cout << "接取失败：" << quests::error_name(result) << '\n'; return false;
        }
        dirty_ = true; std::cout << "接取成功。\n"; show_quests();
    } else if (command == "inspect") {
        arguments(w, 2);
        const auto result = player_->execute_interaction(content_->interactions(), {request_id(), number(w[1])});
        if (result.error != interaction::Error::none) {
            std::cout << "交互失败：" << interaction::error_name(result.error) << " / "
                      << quests::error_name(result.quest_error) << '\n'; return false;
        }
        dirty_ = true; std::cout << "调查完成：路标指向村外，任务事实已提交。\n"; show_quests();
    } else if (command == "claim") {
        arguments(w, 2);
        const auto result = player_->claim_quest_reward({request_id(), number(w[1]), 1});
        if (result.error != quests::Error::none) {
            std::cout << "领奖失败：" << quests::error_name(result.error) << " / "
                      << economy::error_name(result.economy_error) << " / "
                      << inventory::error_name(result.inventory_error) << '\n'; return false;
        }
        dirty_ = true; std::cout << "领奖成功！新手村准备完成。\n"; show_status(); show_quests();
    } else if (command == "talk" || command == "next" || command == "choose" || command == "finish" || command == "cancel") {
        arguments(w, command == "talk" || command == "choose" ? 2 : 1);
        dialogue::Command input{};
        if (command == "talk") {
            if (active_session()) throw std::invalid_argument("已有活动对话，请继续或cancel");
            input.action = dialogue::Action::start; input.dialogue = number(w[1]);
        } else {
            const auto session = active_session();
            if (!session) throw std::invalid_argument("没有进行中的对话");
            const auto view = player_->dialogues().query_dialogue_session(*session);
            input.session = *session; input.expected_node = view->node.id; input.expected_revision = view->revision;
            input.action = command == "next" ? dialogue::Action::advance : command == "choose" ? dialogue::Action::choose
                : command == "finish" ? dialogue::Action::finish : dialogue::Action::cancel;
            if (command == "choose") input.option = number(w[1]);
        }
        input.request_id = request_id();
        const auto result = player_->execute_dialogue_command(dialogue_catalog_, input);
        if (result.error != dialogue::Error::none) {
            std::cout << "对话失败：" << dialogue::error_name(result.error) << " / "
                      << quests::error_name(result.quest_error) << '\n'; return false;
        }
        dirty_ = true;
        if (command == "finish") { std::cout << "对话完成：完成事实已提交任务。\n"; show_quests(); }
        else if (command == "cancel") std::cout << "对话已取消，没有完成事实。\n";
        else show_dialogue();
    } else throw std::invalid_argument("未知命令，输入help查看帮助");
    return true;
}
int Application::run(std::istream& input, bool scripted) {
    help(); show_status(); show_quests();
    std::string line;
    while (!quit_) {
        if (!scripted) std::cout << "\n新手村> " << std::flush;
        if (!std::getline(input, line)) break;
        std::istringstream stream(line); std::vector<std::string> words; std::string word;
        while (stream >> word) words.push_back(word);
        if (words.empty() || words[0][0] == '#') continue;
        if (scripted) std::cout << "\n> " << line << '\n';
        try {
            if (!dispatch(words) && scripted) return 1;
        } catch (const std::exception& e) {
            std::cout << "操作失败：" << e.what() << '\n';
            if (scripted) return 1;
        }
    }
    if (dirty_) std::cout << "退出：本次未保存的进度不会保留；下次请在退出前使用save。\n";
    else std::cout << "退出：进度已保存。\n";
    return 0;
}
} // namespace designer::app
