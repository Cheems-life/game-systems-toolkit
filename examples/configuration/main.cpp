// 配置驱动闭环：从文件选择商品和任务，演示购买、可信目标输入与领奖；不读取/写入真实玩家存档。
#include <designer/configuration/bundle.hpp>
#include <designer/player/player.hpp>
#include <charconv>
#include <iostream>
#include <stdexcept>
namespace config = designer::configuration;
namespace eco = designer::economy;
namespace inv = designer::inventory;
namespace q = designer::quests;
std::uint32_t id(const char* text) {
    const std::string_view s(text);
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(s.data(), s.data() + s.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size() || value == 0)
        throw std::invalid_argument("Expected positive uint32 ID");
    return value;
}
int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 4) {
            std::cerr << "Usage: designer_config_example <game.json> [product-id quest-id]\n"; return 2;
        }
        const auto loaded = config::load(argv[1], q::default_objectives());
        if (loaded.error) {
            std::cerr << loaded.error->source << ": " << loaded.error->path << ": " << loaded.error->message << '\n'; return 1;
        }
        const auto& cfg = *loaded.bundle;
        if (cfg.products().entries().empty() || cfg.quests().entries().empty())
            throw std::runtime_error("Demo needs at least one product and quest");
        const auto product_id = argc == 4 ? id(argv[2]) : cfg.products().entries().begin()->first;
        const auto quest_id = argc == 4 ? id(argv[3]) : cfg.quests().entries().begin()->first;
        const auto* product = cfg.products().find(product_id);
        const auto* quest = cfg.quests().find(quest_id);
        if (!product || !quest) throw std::runtime_error("Selected ID does not exist");
        eco::Wallet wallet(cfg.currencies());
        // 示例初始资产是演示策略，与内容配置分开：只提供这件商品的一份价格。
        for (const auto& cost : product->unit_cost.currencies)
            if (wallet.credit(cost.currency, cost.amount) != eco::Error::none)
                throw std::runtime_error("Demo cannot fund selected product within currency limits");
        auto economy = std::make_unique<eco::Economy>(std::move(wallet),
            std::vector<eco::Container>{{1, inv::Inventory(16, cfg.items())}});
        designer::player::Player player(cfg.products(), cfg.quests(), cfg.objectives(), std::move(economy));
        const auto bought = player.purchase_product({1, product_id, 1, 1});
        if (bought.error != designer::commerce::Error::none)
            throw std::runtime_error(designer::commerce::error_name(bought.error));
        if (player.accept_quest(quest_id, {quest->minimum_level, 0}) != q::Error::none)
            throw std::runtime_error("accept failed");
        // 模拟可信接入层一次性提交完成量；实际游戏须从已验证的玩法事实产生输入。
        q::EventId event = 1;
        for (const auto& goal : quest->objectives)
            if (player.apply_quest_progress_event({event++, {{quest_id, goal.id, goal.target}}}).error != q::Error::none)
                throw std::runtime_error("progress failed");
        const auto claimed = player.claim_quest_reward({2, quest_id, 1});
        if (claimed.error != q::Error::none) throw std::runtime_error(q::error_name(claimed.error));
        if (!player.purchase_product({1, product_id, 1, 1}).replayed || !player.claim_quest_reward({2, quest_id, 1}).replayed)
            throw std::runtime_error("replay failed");
        std::cout << "content_version=" << cfg.content_version() << ": product " << product_id
                  << " purchased, quest " << quest_id << " (" << quest->title << ") claimed; replays succeeded.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
