#pragma once

#include <designer/economy/economy.hpp>

namespace designer::commerce
{

    using ProductId = std::uint32_t;
    using ProductVersion = std::uint32_t;
    using Quantity = std::uint32_t;         // 购买多少份商品
    inline constexpr std::size_t max_products = 4096;
    inline constexpr std::size_t max_history = economy::max_history;

    struct ProductDefinition
    {
        ProductId id;
        ProductVersion version;     // 商品配置版本 (用于区分商品规则变化)
        bool available;             // 上下架状态
        economy::Cost unit_cost;
        economy::RewardBundle unit_reward;
    };

    class ProductCatalog        // 商品配置表
    {
    public:
        // Sorts each list by ID. Rejects duplicates, zero values, empty cost/reward and oversized lists.
        explicit ProductCatalog(std::vector<ProductDefinition> definitions);
        [[nodiscard]] const ProductDefinition *find(ProductId id) const noexcept;
        [[nodiscard]] const std::map<ProductId, ProductDefinition> &entries() const noexcept { return definitions_; }
        void swap(ProductCatalog &other) noexcept;

    private:
        std::map<ProductId, ProductDefinition> definitions_;        // 通过商品 ID 保存定义
    };

    struct PurchaseRequest
    {
        economy::RequestId request_id;
        ProductId product_id;
        Quantity quantity; // Number of bundles, not number of individual items.
        economy::ContainerId destination;
    };
    inline bool operator==(const PurchaseRequest &a, const PurchaseRequest &b) noexcept
    {
        return a.request_id == b.request_id && a.product_id == b.product_id &&
               a.quantity == b.quantity && a.destination == b.destination;
    }

    struct PurchaseReceipt      // 一次成功购买的小票
    {
        PurchaseRequest request;            // 买了什么
        ProductVersion product_version;     // 按哪个商品版本购买
        economy::Receipt settlement;        // 经济系统实际执行的成本、奖励和成功序号
    };
    inline bool operator==(const PurchaseReceipt &a, const PurchaseReceipt &b) noexcept
    {
        return a.request == b.request && a.product_version == b.product_version && a.settlement == b.settlement;
    }

    enum class Error
    {
        none,
        invalid_request,
        unknown_product,
        product_unavailable,
        quantity_overflow,
        history_full,
        request_conflict,
        economy_failure,
        invalid_snapshot
    };
    [[nodiscard]] const char *error_name(Error error) noexcept;

    struct PurchaseResult
    {
        Error error = Error::none;
        economy::Error economy_error = economy::Error::none;
        inventory::Error inventory_error = inventory::Error::none;
        std::optional<PurchaseReceipt> receipt = std::nullopt;      // 可选  失败时通常没有成功凭证
        bool replayed = false;          // 标识是否返回已有购买的结果，而不是新执行了一次购买
    };

    struct Snapshot
    {
        economy::Snapshot economy;
        std::vector<PurchaseReceipt> purchases; // Strictly increasing economic sequence, may have gaps.
    };

    class PurchaseBook;
    struct PreparedPurchase
    {
        PurchaseResult result;
        std::unique_ptr<economy::Economy> economy;
        std::unique_ptr<PurchaseBook> book;
    };

    // Owns purchase records only. Pure preparation never mutates the input economy or this book.
    class PurchaseBook
    {
    public:
        explicit PurchaseBook(std::size_t history_limit = 256);
        [[nodiscard]] PreparedPurchase prepare(const ProductCatalog& catalog, const economy::Economy& economy,
                                               const PurchaseRequest& request) const;
        [[nodiscard]] std::vector<PurchaseReceipt> snapshot() const;
        [[nodiscard]] Error restore(const Snapshot& snapshot);
    private:
        std::map<economy::RequestId, PurchaseReceipt> purchases_;
        std::size_t history_limit_;
    };

    // One player's shop coordinator. Owns the economy, single-threaded.
    // Asset views expire after successful purchase/restore. Catalog views expire after replacement.
    class Shop
    {
    public:
        Shop(ProductCatalog catalog, std::unique_ptr<economy::Economy> economy, std::size_t history_limit = 256);
        Shop(const Shop &) = delete;
        Shop &operator=(const Shop &) = delete;

        [[nodiscard]] const economy::Economy &economy() const noexcept;
        [[nodiscard]] const ProductCatalog &catalog() const noexcept { return catalog_; }

        void replace_catalog(ProductCatalog catalog) noexcept;          // 替换当前商品配置

        [[nodiscard]] PurchaseResult purchase(const PurchaseRequest &request);

        [[nodiscard]] Snapshot snapshot() const;                        // 获取经济状态和购买记录

        // Restoring historical purchases does not consult the CURRENT product catalog.
        [[nodiscard]] Error restore(const Snapshot &snapshot);

    private:
        struct State
        {
            std::unique_ptr<economy::Economy> economy;
            PurchaseBook purchases;
        };
        ProductCatalog catalog_;
        std::unique_ptr<State> state_;
        std::size_t history_limit_;
    };

} // namespace designer::commerce
