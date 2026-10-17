#include <designer/commerce/shop.hpp>
#include "snapshot_validation.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace designer::commerce {
static_assert(std::is_nothrow_move_constructible<PurchaseResult>::value, "Commit must not allocate on return");
namespace {
void validate_currencies(std::vector<economy::CurrencyAmount>& values) {
    if (values.size() > economy::max_entries_per_list) throw std::invalid_argument("Too many product currency entries");
    std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.currency < b.currency; });
    economy::CurrencyId previous = 0;
    for (const auto& value : values) {
        if (value.currency <= previous || value.amount == 0) throw std::invalid_argument("Invalid product currency entry");
        previous = value.currency;
    }
}
bool scale(const ProductDefinition& product, Quantity quantity, economy::Transaction& transaction) {
    transaction.cost = product.unit_cost;
    transaction.reward = product.unit_reward;
    for (auto* list : {&transaction.cost.currencies, &transaction.reward.currencies}) {
        for (auto& value : *list) {
            if (value.amount > std::numeric_limits<economy::Amount>::max() / quantity) return false;
            value.amount *= quantity;
        }
    }
    for (auto& value : transaction.reward.items) {
        if (value.quantity > std::numeric_limits<inventory::Quantity>::max() / quantity) return false;
        value.quantity *= quantity;
    }
    return true;
}
} // namespace

ProductCatalog::ProductCatalog(std::vector<ProductDefinition> definitions) {
    if (definitions.size() > max_products) throw std::invalid_argument("Too many products");
    for (auto& definition : definitions) {
        if (definition.id == 0 || definition.version == 0 || definition.unit_cost.currencies.empty() ||
            (definition.unit_reward.currencies.empty() && definition.unit_reward.items.empty()))
            throw std::invalid_argument("Invalid product definition");
        validate_currencies(definition.unit_cost.currencies);
        validate_currencies(definition.unit_reward.currencies);
        auto& items = definition.unit_reward.items;
        if (items.size() > economy::max_entries_per_list) throw std::invalid_argument("Too many product items");
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.item < b.item; });
        inventory::ItemId previous = 0;
        for (const auto& item : items) {
            if (item.item <= previous || item.quantity == 0) throw std::invalid_argument("Invalid product item entry");
            previous = item.item;
        }
        const auto id = definition.id;
        if (!definitions_.emplace(id, std::move(definition)).second) throw std::invalid_argument("Duplicate product ID");
    }
}
const ProductDefinition* ProductCatalog::find(ProductId id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}
void ProductCatalog::swap(ProductCatalog& other) noexcept { definitions_.swap(other.definitions_); }
const char* error_name(Error error) noexcept {
    switch (error) {
    case Error::none: return "none";
    case Error::invalid_request: return "invalid_request";
    case Error::unknown_product: return "unknown_product";
    case Error::product_unavailable: return "product_unavailable";
    case Error::quantity_overflow: return "quantity_overflow";
    case Error::history_full: return "history_full";
    case Error::request_conflict: return "request_conflict";
    case Error::economy_failure: return "economy_failure";
    case Error::invalid_snapshot: return "invalid_snapshot";
    }
    return "unrecognized_error";
}
Shop::Shop(ProductCatalog catalog, std::unique_ptr<economy::Economy> economy, std::size_t history_limit)
    : catalog_(std::move(catalog)), state_(std::make_unique<State>(State{std::move(economy), {}})), history_limit_(history_limit) {
    if (!state_->economy || history_limit == 0 || history_limit > max_history)
        throw std::invalid_argument("Invalid shop economy or history limit");
}
const economy::Economy& Shop::economy() const noexcept { return *state_->economy; }
void Shop::replace_catalog(ProductCatalog catalog) noexcept { catalog_.swap(catalog); }
PurchaseResult Shop::purchase(const PurchaseRequest& request) {
    if (!detail::valid_request(request)) return {Error::invalid_request};
    const auto previous = state_->purchases.find(request.request_id);
    if (previous != state_->purchases.end()) {
        if (!(previous->second.request == request)) return {Error::request_conflict};
        return {Error::none, economy::Error::none, inventory::Error::none, previous->second, true};
    }
    if (state_->purchases.size() >= history_limit_) return {Error::history_full};
    const auto* product = catalog_.find(request.product_id);
    if (!product) return {Error::unknown_product};
    if (!product->available) return {Error::product_unavailable};
    economy::Transaction transaction{request.request_id, request.destination, {}, {}};
    if (!scale(*product, request.quantity, transaction)) return {Error::quantity_overflow};
    auto candidate = std::make_unique<State>(State{state_->economy->clone(), state_->purchases});
    const auto result = candidate->economy->execute(transaction);
    // An economic request ID from another operation must never be adopted as a shop purchase.
    if (result.error == economy::Error::request_conflict || result.replayed) return {Error::request_conflict};
    if (result.error != economy::Error::none) return {Error::economy_failure, result.error, result.inventory_error};
    PurchaseReceipt receipt{request, product->version, *result.receipt};
    candidate->purchases.emplace(request.request_id, receipt);
    PurchaseResult output{Error::none, economy::Error::none, inventory::Error::none, std::move(receipt), false};
    state_.swap(candidate);
    return output;
}
Snapshot Shop::snapshot() const {
    Snapshot result{state_->economy->snapshot(), {}};
    for (const auto& entry : state_->purchases) result.purchases.push_back(entry.second);
    std::sort(result.purchases.begin(), result.purchases.end(),
              [](const auto& a, const auto& b) { return a.settlement.sequence < b.settlement.sequence; });
    return result;
}
Error Shop::restore(const Snapshot& snapshot) {
    if (snapshot.purchases.size() > history_limit_ || !detail::valid_links(snapshot)) return Error::invalid_snapshot;
    auto candidate = std::make_unique<State>(State{state_->economy->clone(), {}});
    if (candidate->economy->restore(snapshot.economy) != economy::Error::none) return Error::invalid_snapshot;
    for (const auto& receipt : snapshot.purchases) candidate->purchases.emplace(receipt.request.request_id, receipt);
    state_.swap(candidate);
    return Error::none;
}

} // namespace designer::commerce
