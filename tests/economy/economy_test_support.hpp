// 经济测试公共辅助：提供 CHECK 检查、示例经济对象、交易请求、物品统计和快照比较。
// failed 辅助函数同时检查错误码及失败后状态不变；本头文件没有 main，不单独作为测试运行。

#pragma once
#include <designer/economy/archive.hpp>
#include <cstdlib>
#include <iostream>

namespace eco = designer::economy;
namespace inv = designer::inventory;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Check failed at " << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (false)
inline eco::Economy make_economy(std::size_t limit = 32, std::size_t slots = 3) {
    eco::Wallet wallet(eco::CurrencyCatalog({{1, 1000}, {2, 100}}));
    CHECK(wallet.credit(1, 500) == eco::Error::none);
    CHECK(wallet.credit(2, 90) == eco::Error::none);
    inv::ItemCatalog rules({{10, 10}, {20, 1}});
    return eco::Economy(std::move(wallet), {{1, inv::Inventory(slots, rules)}, {2, inv::Inventory(5, rules)}}, limit);
}
inline bool equal(const eco::Snapshot& a, const eco::Snapshot& b) {
    if (a.balances != b.balances || a.receipts != b.receipts || a.containers.size() != b.containers.size()) return false;
    for (std::size_t i = 0; i < a.containers.size(); ++i)
        if (a.containers[i].id != b.containers[i].id || a.containers[i].slots != b.containers[i].slots) return false;
    return true;
}
inline std::uint64_t total(const inv::Inventory& bag, inv::ItemId item) {
    std::uint64_t quantity = 0;
    for (const auto& slot : bag.slots()) if (slot && slot->item_id == item) quantity += slot->quantity;
    return quantity;
}
inline eco::Transaction purchase(eco::RequestId id = 1) {
    return {id, 1, {{{1, 100}}}, {{{2, 5}}, {{10, 3}, {10, 2}}}};
}
inline void failed(eco::Economy& economy, const eco::Transaction& request, eco::Error expected,
                   inv::Error inventory_error = inv::Error::none) {
    const auto before = economy.snapshot();
    const auto result = economy.execute(request);
    CHECK(result.error == expected && result.inventory_error == inventory_error);
    CHECK(!result.receipt && !result.replayed);
    CHECK(equal(before, economy.snapshot()));
}
