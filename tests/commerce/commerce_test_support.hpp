// 商城测试公共辅助：复用经济测试工具，创建示例商品目录和商城，比较商城完整快照。
// failed 辅助函数检查商城/经济/背包错误及状态不变；本头文件没有 main，不单独作为测试运行。

#pragma once
#include "../economy/economy_test_support.hpp"
#include <designer/commerce/archive.hpp>

namespace commerce = designer::commerce;
inline commerce::ProductCatalog products(eco::Amount price = 100, std::uint32_t version = 1, bool available = true) {
    return commerce::ProductCatalog({{100, version, available, {{{1, price}}}, {{{2, 1}}, {{10, 5}}}},
                                     {200, 1, true, {{{1, 100}}}, {{}, {{20, 1}}}}});
}
inline commerce::Shop make_shop(std::size_t shop_limit = 32, std::size_t economic_limit = 32, std::size_t slots = 3) {
    return commerce::Shop(products(), make_economy(economic_limit, slots).clone(), shop_limit);
}
inline bool equal(const commerce::Snapshot& a, const commerce::Snapshot& b) {
    return equal(a.economy, b.economy) && a.purchases == b.purchases;
}
inline void failed(commerce::Shop& shop, const commerce::PurchaseRequest& request, commerce::Error error,
                   eco::Error economic_error = eco::Error::none, inv::Error inventory_error = inv::Error::none) {
    const auto before = shop.snapshot();
    const auto result = shop.purchase(request);
    CHECK(result.error == error && result.economy_error == economic_error && result.inventory_error == inventory_error);
    CHECK(!result.receipt && !result.replayed && equal(before, shop.snapshot()));
}
