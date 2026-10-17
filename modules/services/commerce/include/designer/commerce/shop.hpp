#pragma once

#include <designer/economy/economy.hpp>

namespace designer::commerce {

using ProductId = std::uint32_t;
using ProductVersion = std::uint32_t;
using Quantity = std::uint32_t;
inline constexpr std::size_t max_products = 4096;
inline constexpr std::size_t max_history = economy::max_history;

struct ProductDefinition {
    ProductId id;
    ProductVersion version;
    bool available;
    economy::Cost unit_cost;
    economy::RewardBundle unit_reward;
};
class ProductCatalog {
public:
    // Sorts each list by ID. Rejects duplicates, zero values, empty cost/reward and oversized lists.
    explicit ProductCatalog(std::vector<ProductDefinition> definitions);
    [[nodiscard]] const ProductDefinition* find(ProductId id) const noexcept;
    [[nodiscard]] const std::map<ProductId, ProductDefinition>& entries() const noexcept { return definitions_; }
    void swap(ProductCatalog& other) noexcept;
private:
    std::map<ProductId, ProductDefinition> definitions_;
};
struct PurchaseRequest {
    economy::RequestId request_id;
    ProductId product_id;
    Quantity quantity; // Number of bundles, not number of individual items.
    economy::ContainerId destination;
};
inline bool operator==(const PurchaseRequest& a, const PurchaseRequest& b) noexcept {
    return a.request_id == b.request_id && a.product_id == b.product_id &&
        a.quantity == b.quantity && a.destination == b.destination;
}
struct PurchaseReceipt {
    PurchaseRequest request;
    ProductVersion product_version;
    economy::Receipt settlement;
};
inline bool operator==(const PurchaseReceipt& a, const PurchaseReceipt& b) noexcept {
    return a.request == b.request && a.product_version == b.product_version && a.settlement == b.settlement;
}
enum class Error {
    none, invalid_request, unknown_product, product_unavailable, quantity_overflow,
    history_full, request_conflict, economy_failure, invalid_snapshot
};
[[nodiscard]] const char* error_name(Error error) noexcept;
struct PurchaseResult {
    Error error = Error::none;
    economy::Error economy_error = economy::Error::none;
    inventory::Error inventory_error = inventory::Error::none;
    std::optional<PurchaseReceipt> receipt = std::nullopt;
    bool replayed = false;
};
struct Snapshot {
    economy::Snapshot economy;
    std::vector<PurchaseReceipt> purchases; // Strictly increasing economic sequence, may have gaps.
};

// One player's shop coordinator. Owns the economy, single-threaded.
// Asset views expire after successful purchase/restore. Catalog views expire after replacement.
class Shop {
public:
    Shop(ProductCatalog catalog, std::unique_ptr<economy::Economy> economy, std::size_t history_limit = 256);
    Shop(const Shop&) = delete;
    Shop& operator=(const Shop&) = delete;
    [[nodiscard]] const economy::Economy& economy() const noexcept;
    [[nodiscard]] const ProductCatalog& catalog() const noexcept { return catalog_; }
    void replace_catalog(ProductCatalog catalog) noexcept;
    [[nodiscard]] PurchaseResult purchase(const PurchaseRequest& request);
    [[nodiscard]] Snapshot snapshot() const;
    // Restoring historical purchases does not consult the CURRENT product catalog.
    [[nodiscard]] Error restore(const Snapshot& snapshot);
private:
    struct State {
        std::unique_ptr<economy::Economy> economy;
        std::map<economy::RequestId, PurchaseReceipt> purchases;
    };
    ProductCatalog catalog_;
    std::unique_ptr<State> state_;
    std::size_t history_limit_;
};

} // namespace designer::commerce
