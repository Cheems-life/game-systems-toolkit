#include <designer/economy/economy.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <type_traits>

namespace designer::economy {
static_assert(std::is_nothrow_move_constructible<ExecuteResult>::value, "Commit must not allocate on return");
namespace {
Error merge_currencies(std::vector<CurrencyAmount>& values) {
    if (values.size() > max_entries_per_list) return Error::invalid_request;
    std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.currency < b.currency; });
    std::vector<CurrencyAmount> merged;
    for (const auto& value : values) {
        if (value.currency == 0 || value.amount == 0) return Error::invalid_amount;
        if (!merged.empty() && merged.back().currency == value.currency) {
            if (value.amount > std::numeric_limits<Amount>::max() - merged.back().amount) return Error::amount_overflow;
            merged.back().amount += value.amount;
        } else merged.push_back(value);
    }
    values.swap(merged);
    return Error::none;
}
Error normalize(Transaction& request) {
    if (request.request_id == 0 || request.destination == 0 || request.reward.items.size() > max_entries_per_list)
        return Error::invalid_request;
    auto result = merge_currencies(request.cost.currencies);
    if (result != Error::none) return result;
    result = merge_currencies(request.reward.currencies);
    if (result != Error::none) return result;
    std::sort(request.reward.items.begin(), request.reward.items.end(),
              [](const auto& a, const auto& b) { return a.item < b.item; });
    std::vector<ItemAmount> items;
    for (const auto& value : request.reward.items) {
        if (value.item == 0 || value.quantity == 0) return Error::invalid_amount;
        if (!items.empty() && items.back().item == value.item) {
            if (value.quantity > std::numeric_limits<inventory::Quantity>::max() - items.back().quantity)
                return Error::amount_overflow;
            items.back().quantity += value.quantity;
        } else items.push_back(value);
    }
    request.reward.items.swap(items);
    if (request.cost.currencies.empty() && request.reward.currencies.empty() && request.reward.items.empty())
        return Error::invalid_request;
    return Error::none;
}
} // namespace

CurrencyCatalog::CurrencyCatalog(std::vector<CurrencyDefinition> definitions) {
    if (definitions.size() > max_currencies) throw std::invalid_argument("Too many currencies");
    for (const auto& definition : definitions) {
        if (definition.id == 0 || definition.max_balance == 0 ||
            !definitions_.emplace(definition.id, definition).second)
            throw std::invalid_argument("Invalid or duplicate currency definition");
    }
}
const CurrencyDefinition* CurrencyCatalog::find(CurrencyId id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}
const char* error_name(Error error) noexcept {
    switch (error) {
    case Error::none: return "none";
    case Error::invalid_amount: return "invalid_amount";
    case Error::unknown_currency: return "unknown_currency";
    case Error::insufficient_funds: return "insufficient_funds";
    case Error::balance_limit_exceeded: return "balance_limit_exceeded";
    case Error::amount_overflow: return "amount_overflow";
    case Error::invalid_request: return "invalid_request";
    case Error::unknown_container: return "unknown_container";
    case Error::inventory_failure: return "inventory_failure";
    case Error::request_conflict: return "request_conflict";
    case Error::history_full: return "history_full";
    case Error::invalid_snapshot: return "invalid_snapshot";
    }
    return "unrecognized_error";
}
Wallet::Wallet(CurrencyCatalog catalog) : catalog_(std::move(catalog)) {}
Amount Wallet::balance(CurrencyId id) const noexcept {
    const auto found = balances_.find(id);
    return found == balances_.end() ? 0 : found->second;
}
const CurrencyDefinition* Wallet::definition(CurrencyId id) const noexcept { return catalog_.find(id); }
Error Wallet::credit(CurrencyId id, Amount amount) {
    if (amount == 0) return Error::invalid_amount;
    const auto* rule = catalog_.find(id);
    if (!rule) return Error::unknown_currency;
    const auto current = balance(id);
    if (amount > rule->max_balance - current) return Error::balance_limit_exceeded;
    balances_[id] = current + amount;
    return Error::none;
}
Error Wallet::debit(CurrencyId id, Amount amount) {
    if (amount == 0) return Error::invalid_amount;
    if (!catalog_.find(id)) return Error::unknown_currency;
    const auto current = balance(id);
    if (amount > current) return Error::insufficient_funds;
    if (amount == current) balances_.erase(id);
    else balances_.find(id)->second = current - amount;
    return Error::none;
}
Error Wallet::restore(const WalletSnapshot& snapshot) {
    if (snapshot.size() > max_currencies) return Error::invalid_snapshot;
    for (const auto& entry : snapshot) {
        const auto* rule = catalog_.find(entry.first);
        if (!rule) return Error::unknown_currency;
        if (entry.second == 0) return Error::invalid_amount;
        if (entry.second > rule->max_balance) return Error::balance_limit_exceeded;
    }
    auto candidate = snapshot;
    balances_.swap(candidate);
    return Error::none;
}

Economy::Economy(Wallet wallet, std::vector<Container> containers, std::size_t history_limit)
    : state_(std::make_unique<State>(State{std::move(wallet), {}, {}})), history_limit_(history_limit) {
    if (history_limit == 0 || history_limit > max_history || containers.size() > inventory::archive_max_containers)
        throw std::invalid_argument("Invalid economy limits");
    std::size_t total_slots = 0;
    for (auto& container : containers) {
        const auto slots = container.inventory.slots().size();
        if (container.id == 0 || slots > inventory::archive_max_slots_per_container ||
            slots > inventory::archive_max_total_slots - total_slots ||
            !state_->containers.emplace(container.id, std::move(container.inventory)).second)
            throw std::invalid_argument("Invalid or duplicate container");
        total_slots += slots;
    }
}
const Wallet& Economy::wallet() const noexcept { return state_->wallet; }
const inventory::Inventory* Economy::container(ContainerId id) const noexcept {
    const auto found = state_->containers.find(id);
    return found == state_->containers.end() ? nullptr : &found->second;
}
std::size_t Economy::history_size() const noexcept { return state_->history.size(); }

ExecuteResult Economy::execute(const Transaction& input) {
    // Bound raw input before copying. Canonical content defines replay equality.
    if (input.cost.currencies.size() > max_entries_per_list || input.reward.currencies.size() > max_entries_per_list ||
        input.reward.items.size() > max_entries_per_list) return {Error::invalid_request};
    auto request = input;
    const auto normalized = normalize(request);
    if (normalized != Error::none) return {normalized};
    const auto previous = state_->history.find(request.request_id);
    if (previous != state_->history.end()) {
        if (!(previous->second.transaction == request)) return {Error::request_conflict};
        return {Error::none, inventory::Error::none, previous->second, true};
    }
    if (state_->history.size() >= history_limit_) return {Error::history_full};
    if (state_->containers.find(request.destination) == state_->containers.end()) return {Error::unknown_container};
    auto candidate = std::make_unique<State>(*state_);
    for (const auto& cost : request.cost.currencies) {
        const auto result = candidate->wallet.debit(cost.currency, cost.amount);
        if (result != Error::none) return {result};
    }
    for (const auto& reward : request.reward.currencies) {
        const auto result = candidate->wallet.credit(reward.currency, reward.amount);
        if (result != Error::none) return {result};
    }
    auto& destination = candidate->containers.at(request.destination);
    for (const auto& reward : request.reward.items) {
        const auto result = destination.add(reward.item, reward.quantity);
        if (result != inventory::Error::none) return {Error::inventory_failure, result};
    }
    Receipt receipt{request, static_cast<std::uint64_t>(candidate->history.size()) + 1};
    candidate->history.emplace(request.request_id, receipt);
    // Prepare return allocations before commit, too. Returning the local uses noexcept moves.
    ExecuteResult result{Error::none, inventory::Error::none, std::move(receipt), false};
    state_.swap(candidate);
    return result;
}
Snapshot Economy::snapshot() const {
    Snapshot result;
    result.balances = state_->wallet.snapshot();
    for (const auto& entry : state_->containers) result.containers.push_back({entry.first, entry.second.snapshot()});
    for (const auto& entry : state_->history) result.receipts.push_back(entry.second);
    std::sort(result.receipts.begin(), result.receipts.end(), [](const auto& a, const auto& b) { return a.sequence < b.sequence; });
    return result;
}
Error Economy::restore(const Snapshot& snapshot) {
    if (snapshot.receipts.size() > history_limit_) return Error::invalid_snapshot;
    auto candidate = std::make_unique<State>(*state_);
    const auto wallet_result = candidate->wallet.restore(snapshot.balances);
    if (wallet_result != Error::none) return wallet_result;
    std::vector<inventory::ContainerBinding> bindings;
    for (auto& entry : candidate->containers) bindings.push_back({entry.first, &entry.second});
    if (inventory::restore_archive(snapshot.containers, bindings).error != inventory::ArchiveError::none)
        return Error::invalid_snapshot;
    candidate->history.clear();
    for (std::size_t i = 0; i < snapshot.receipts.size(); ++i) {
        const auto& receipt = snapshot.receipts[i];
        if (receipt.transaction.cost.currencies.size() > max_entries_per_list ||
            receipt.transaction.reward.currencies.size() > max_entries_per_list ||
            receipt.transaction.reward.items.size() > max_entries_per_list) return Error::invalid_snapshot;
        auto canonical = receipt.transaction;
        if (normalize(canonical) != Error::none || !(canonical == receipt.transaction) || receipt.sequence != i + 1 ||
            candidate->containers.find(canonical.destination) == candidate->containers.end() ||
            candidate->history.find(canonical.request_id) != candidate->history.end()) return Error::invalid_snapshot;
        for (const auto& list : {&canonical.cost.currencies, &canonical.reward.currencies}) {
            for (const auto& entry : *list) {
                const auto* rule = candidate->wallet.definition(entry.currency);
                if (!rule || entry.amount > rule->max_balance) return Error::invalid_snapshot;
            }
        }
        // Validate historical item IDs against current rules, independently of current contents.
        auto empty = candidate->containers.at(canonical.destination);
        if (empty.restore(inventory::Snapshot(empty.slots().size())) != inventory::Error::none)
            return Error::invalid_snapshot;
        for (const auto& entry : canonical.reward.items)
            if (empty.add(entry.item, entry.quantity) != inventory::Error::none) return Error::invalid_snapshot;
        candidate->history.emplace(canonical.request_id, receipt);
    }
    state_.swap(candidate);
    return Error::none;
}

} // namespace designer::economy
