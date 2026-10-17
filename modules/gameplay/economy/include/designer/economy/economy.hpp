#pragma once

#include <designer/inventory/archive.hpp>
#include <map>
#include <memory>
#include <optional>

namespace designer::economy
{

    using CurrencyId = std::uint32_t;
    using Amount = std::uint64_t;
    using RequestId = std::uint64_t;
    using ContainerId = inventory::ContainerId;
    inline constexpr std::size_t max_currencies = 1024;
    inline constexpr std::size_t max_entries_per_list = 64;
    inline constexpr std::size_t max_history = 4096;

    // 货币规则表
    struct CurrencyDefinition
    {
        CurrencyId id;
        Amount max_balance;     // 最多能持有多少
    };
    struct CurrencyAmount       // 本次涉及多少物品
    {
        CurrencyId currency;
        Amount amount;
    };
    struct ItemAmount           // 本次涉及多少钱
    {
        inventory::ItemId item;
        inventory::Quantity quantity;
    };
    inline bool operator==(const CurrencyAmount &a, const CurrencyAmount &b) noexcept
    {
        return a.currency == b.currency && a.amount == b.amount;
    }
    inline bool operator==(const ItemAmount &a, const ItemAmount &b) noexcept
    {
        return a.item == b.item && a.quantity == b.quantity;
    }

    class CurrencyCatalog
    {
    public:
        // Rejects zero IDs/limits, duplicates and more than max_currencies definitions.
        explicit CurrencyCatalog(std::vector<CurrencyDefinition> definitions);
        [[nodiscard]] const CurrencyDefinition *find(CurrencyId id) const noexcept;

    private:
        std::map<CurrencyId, CurrencyDefinition> definitions_;
    };

    enum class Error
    {
        none,
        invalid_amount,
        unknown_currency,
        insufficient_funds,
        balance_limit_exceeded,
        amount_overflow,
        invalid_request,
        unknown_container,
        inventory_failure,
        request_conflict,
        history_full,
        invalid_snapshot
    };
    [[nodiscard]] const char *error_name(Error error) noexcept;
    using WalletSnapshot = std::map<CurrencyId, Amount>;

    class Wallet    // 只处理货币增减
    {
    public:
        explicit Wallet(CurrencyCatalog catalog);
        // Unknown currency also reads as zero. Mutations always reject unknown IDs.
        [[nodiscard]] Amount balance(CurrencyId id) const noexcept;
        [[nodiscard]] const CurrencyDefinition *definition(CurrencyId id) const noexcept;
        [[nodiscard]] Error credit(CurrencyId id, Amount amount);   // 增加
        [[nodiscard]] Error debit(CurrencyId id, Amount amount);    // 减少
        [[nodiscard]] WalletSnapshot snapshot() const { return balances_; }
        // Stored entries must be known, positive and within their limits; absence = zero.
        [[nodiscard]] Error restore(const WalletSnapshot &snapshot);

    private:
        CurrencyCatalog catalog_;
        WalletSnapshot balances_;
    };

    struct Cost
    {
        std::vector<CurrencyAmount> currencies;
    };
    struct RewardBundle
    {
        std::vector<CurrencyAmount> currencies;
        std::vector<ItemAmount> items;
    };
    struct Transaction
    {
        RequestId request_id;       // 请求编号
        ContainerId destination;    // 目标容器
        Cost cost;                  // 消耗
        RewardBundle reward;        // 奖励
    };
    inline bool operator==(const Transaction &a, const Transaction &b) noexcept
    {
        return a.request_id == b.request_id && a.destination == b.destination &&
               a.cost.currencies == b.cost.currencies && a.reward.currencies == b.reward.currencies &&
               a.reward.items == b.reward.items;
    }

    // 一次成功交易的凭证
    struct Receipt
    {
        Transaction transaction;    // 这次交易的完整内容
        std::uint64_t sequence;     // 成功交易的顺序编号
    };
    inline bool operator==(const Receipt &a, const Receipt &b) noexcept
    {
        return a.sequence == b.sequence && a.transaction == b.transaction;
    }
    struct ExecuteResult
    {
        Error error = Error::none;
        inventory::Error inventory_error = inventory::Error::none;
        std::optional<Receipt> receipt = std::nullopt;
        bool replayed = false;
    };
    struct Container
    {
        ContainerId id;
        inventory::Inventory inventory;
    };
    struct Snapshot
    {
        WalletSnapshot balances;
        inventory::Archive containers;
        std::vector<Receipt> receipts; // Strict sequence order: 1, 2, ...; no eviction.
    };

    // Owns one player's assets and successful-request history. Single-threaded.
    // Returned views are invalidated by successful execute/restore; use snapshots to retain data.
    class Economy
    {
    public:
        Economy(Wallet wallet, std::vector<Container> containers, std::size_t history_limit = 256);
        Economy(const Economy &) = delete;
        Economy &operator=(const Economy &) = delete;
        [[nodiscard]] const Wallet &wallet() const noexcept;
        [[nodiscard]] const inventory::Inventory *container(ContainerId id) const noexcept;
        [[nodiscard]] std::size_t history_size() const noexcept;
        [[nodiscard]] ExecuteResult execute(const Transaction &request);
        [[nodiscard]] Snapshot snapshot() const;
        // All assets and records are validated and staged before a noexcept pointer swap.
        [[nodiscard]] Error restore(const Snapshot &snapshot);

    private:
        struct State
        {
            Wallet wallet;                                          // 钱包
            std::map<ContainerId, inventory::Inventory> containers; // 容器
            std::map<RequestId, Receipt> history;                   // 历史记录
        };
        std::unique_ptr<State> state_;
        std::size_t history_limit_;
    };

} // namespace designer::economy
