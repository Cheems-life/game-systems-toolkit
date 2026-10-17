#include <designer/commerce/shop.hpp>
#include "snapshot_validation.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace designer::commerce
{
    static_assert(std::is_nothrow_move_constructible<PurchaseResult>::value, "Commit must not allocate on return");
    namespace
    {
        void validate_currencies(std::vector<economy::CurrencyAmount> &values)
        {
            if (values.size() > economy::max_entries_per_list)
                throw std::invalid_argument("Too many product currency entries");
            std::sort(values.begin(), values.end(), [](const auto &a, const auto &b)
                      { return a.currency < b.currency; });
            economy::CurrencyId previous = 0;
            for (const auto &value : values)
            {
                if (value.currency <= previous || value.amount == 0)
                    throw std::invalid_argument("Invalid product currency entry");
                previous = value.currency;
            }
        }
        bool scale(const ProductDefinition &product, Quantity quantity, economy::Transaction &transaction)
        {
            transaction.cost = product.unit_cost;
            transaction.reward = product.unit_reward;
            for (auto *list : {&transaction.cost.currencies, &transaction.reward.currencies})
            {
                for (auto &value : *list)
                {
                    if (value.amount > std::numeric_limits<economy::Amount>::max() / quantity)
                        return false;
                    value.amount *= quantity;
                }
            }
            for (auto &value : transaction.reward.items)
            {
                if (value.quantity > std::numeric_limits<inventory::Quantity>::max() / quantity)
                    return false;
                value.quantity *= quantity;
            }
            return true;
        }
    } // namespace

    ProductCatalog::ProductCatalog(std::vector<ProductDefinition> definitions)
    {
        if (definitions.size() > max_products)
            throw std::invalid_argument("Too many products");
        for (auto &definition : definitions)
        {
            if (definition.id == 0 || definition.version == 0 || definition.unit_cost.currencies.empty() ||
                (definition.unit_reward.currencies.empty() && definition.unit_reward.items.empty()))
                throw std::invalid_argument("Invalid product definition");
            validate_currencies(definition.unit_cost.currencies);
            validate_currencies(definition.unit_reward.currencies);
            auto &items = definition.unit_reward.items;
            if (items.size() > economy::max_entries_per_list)
                throw std::invalid_argument("Too many product items");
            std::sort(items.begin(), items.end(), [](const auto &a, const auto &b)
                      { return a.item < b.item; });
            inventory::ItemId previous = 0;
            for (const auto &item : items)
            {
                if (item.item <= previous || item.quantity == 0)
                    throw std::invalid_argument("Invalid product item entry");
                previous = item.item;
            }
            const auto id = definition.id;
            if (!definitions_.emplace(id, std::move(definition)).second)
                throw std::invalid_argument("Duplicate product ID");
        }
    }
    const ProductDefinition *ProductCatalog::find(ProductId id) const noexcept
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() ? nullptr : &found->second;
    }
    void ProductCatalog::swap(ProductCatalog &other) noexcept { definitions_.swap(other.definitions_); }
    const char *error_name(Error error) noexcept
    {
        switch (error)
        {
        case Error::none:
            return "none";
        case Error::invalid_request:
            return "invalid_request";
        case Error::unknown_product:
            return "unknown_product";
        case Error::product_unavailable:
            return "product_unavailable";
        case Error::quantity_overflow:
            return "quantity_overflow";
        case Error::history_full:
            return "history_full";
        case Error::request_conflict:
            return "request_conflict";
        case Error::economy_failure:
            return "economy_failure";
        case Error::invalid_snapshot:
            return "invalid_snapshot";
        }
        return "unrecognized_error";
    }
    Shop::Shop(ProductCatalog catalog, std::unique_ptr<economy::Economy> economy, std::size_t history_limit)
        : catalog_(std::move(catalog)), state_(std::make_unique<State>(State{std::move(economy), PurchaseBook(history_limit)})), history_limit_(history_limit)
    {
        if (!state_->economy || history_limit == 0 || history_limit > max_history)
            throw std::invalid_argument("Invalid shop economy or history limit");
    }

    const economy::Economy &Shop::economy() const noexcept {
        return *state_->economy;
    }

    void Shop::replace_catalog(ProductCatalog catalog) noexcept {
        catalog_.swap(catalog);
    }

    PurchaseBook::PurchaseBook(std::size_t history_limit) : history_limit_(history_limit)
    {
        if (history_limit == 0 || history_limit > max_history)
            throw std::invalid_argument("Invalid purchase history limit");
    }

    PreparedPurchase PurchaseBook::prepare(const ProductCatalog& catalog, const economy::Economy& economy,
                                           const PurchaseRequest& request) const
    {
        if (!detail::valid_request(request)) return {{Error::invalid_request}, nullptr, nullptr};
        const auto previous = purchases_.find(request.request_id);
        if (previous != purchases_.end()) {
            if (!(previous->second.request == request)) return {{Error::request_conflict}, nullptr, nullptr};
            return {{Error::none, economy::Error::none, inventory::Error::none, previous->second, true}, nullptr, nullptr};
        }
        if (purchases_.size() >= history_limit_) return {{Error::history_full}, nullptr, nullptr};
        const auto* product = catalog.find(request.product_id);
        if (!product) return {{Error::unknown_product}, nullptr, nullptr};
        if (!product->available) return {{Error::product_unavailable}, nullptr, nullptr};
        economy::Transaction transaction{request.request_id, request.destination, {}, {}};
        if (!scale(*product, request.quantity, transaction)) return {{Error::quantity_overflow}, nullptr, nullptr};
        auto candidate_economy = economy.clone();
        auto candidate_book = std::make_unique<PurchaseBook>(*this);
        const auto result = candidate_economy->execute(transaction);
        if (result.error == economy::Error::request_conflict || result.replayed)
            return {{Error::request_conflict}, nullptr, nullptr};
        if (result.error != economy::Error::none)
            return {{Error::economy_failure, result.error, result.inventory_error}, nullptr, nullptr};
        PurchaseReceipt receipt{request, product->version, *result.receipt};
        candidate_book->purchases_.emplace(request.request_id, receipt);
        PurchaseResult output{Error::none, economy::Error::none, inventory::Error::none, std::move(receipt), false};
        return {std::move(output), std::move(candidate_economy), std::move(candidate_book)};
    }

    std::vector<PurchaseReceipt> PurchaseBook::snapshot() const
    {
        std::vector<PurchaseReceipt> result;
        for (const auto& entry : purchases_) result.push_back(entry.second);
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.settlement.sequence < b.settlement.sequence;
        });
        return result;
    }

    Error PurchaseBook::restore(const Snapshot& snapshot)
    {
        if (snapshot.purchases.size() > history_limit_ || !detail::valid_links(snapshot)) return Error::invalid_snapshot;
        std::map<economy::RequestId, PurchaseReceipt> candidate;
        for (const auto& receipt : snapshot.purchases) candidate.emplace(receipt.request.request_id, receipt);
        purchases_.swap(candidate);
        return Error::none;
    }

    PurchaseResult Shop::purchase(const PurchaseRequest& request)
    {
        auto prepared = state_->purchases.prepare(catalog_, *state_->economy, request);
        if (prepared.result.error != Error::none || prepared.result.replayed) return std::move(prepared.result);
        auto candidate = std::make_unique<State>(State{std::move(prepared.economy), std::move(*prepared.book)});
        state_.swap(candidate);
        return std::move(prepared.result);
    }

    Snapshot Shop::snapshot() const
    {
        return {state_->economy->snapshot(), state_->purchases.snapshot()};
    }

    Error Shop::restore(const Snapshot& snapshot)
    {
        auto candidate = std::make_unique<State>(State{state_->economy->clone(), PurchaseBook(history_limit_)});
        if (candidate->economy->restore(snapshot.economy) != economy::Error::none ||
            candidate->purchases.restore(snapshot) != Error::none) return Error::invalid_snapshot;
        state_.swap(candidate);
        return Error::none;
    }

} // namespace designer::commerce
