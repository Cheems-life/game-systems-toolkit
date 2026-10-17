#pragma once

#include <designer/commerce/shop.hpp>
#include <set>

namespace designer::commerce::detail {
inline bool valid_request(const PurchaseRequest& request) noexcept {
    return request.request_id != 0 && request.product_id != 0 && request.quantity != 0 && request.destination != 0;
}
// Check historical intent against the frozen economic receipt, never today's product configuration.
inline bool valid_links(const Snapshot& snapshot) {
    if (snapshot.purchases.size() > max_history || snapshot.economy.receipts.size() > economy::max_history) return false;
    std::map<economy::RequestId, const economy::Receipt*> economic_receipts;
    for (const auto& receipt : snapshot.economy.receipts)
        if (!economic_receipts.emplace(receipt.transaction.request_id, &receipt).second) return false;
    std::set<economy::RequestId> seen;
    std::uint64_t previous_sequence = 0;
    for (const auto& purchase : snapshot.purchases) {
        const auto& request = purchase.request;
        const auto& settlement = purchase.settlement;
        const auto& transaction = settlement.transaction;
        if (!valid_request(request) || purchase.product_version == 0 ||
            !seen.insert(request.request_id).second || settlement.sequence <= previous_sequence ||
            request.request_id != transaction.request_id || request.destination != transaction.destination ||
            transaction.cost.currencies.empty() ||
            (transaction.reward.currencies.empty() && transaction.reward.items.empty())) return false;
        previous_sequence = settlement.sequence;
        const auto found = economic_receipts.find(request.request_id);
        if (found == economic_receipts.end() || !(*found->second == settlement)) return false;
        // A historical bundle must be expressible as positive unit amounts times quantity.
        for (const auto* list : {&transaction.cost.currencies, &transaction.reward.currencies})
            for (const auto& value : *list)
                if (value.amount == 0 || value.amount % request.quantity != 0) return false;
        for (const auto& value : transaction.reward.items)
            if (value.quantity == 0 || value.quantity % request.quantity != 0) return false;
    }
    return true;
}
} // namespace designer::commerce::detail
