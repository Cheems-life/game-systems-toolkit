#include <utility>
#include <designer/quests/player.hpp>
#include <array>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace designer::quests {
static_assert(std::is_nothrow_move_constructible<ClaimResult>::value, "Claim return must not allocate after commit");
Player::Player(commerce::ProductCatalog products, QuestCatalog quests, ObjectiveRegistry registry,
               std::unique_ptr<economy::Economy> economy, std::size_t event_limit, std::size_t purchase_limit)
    : products_(std::move(products)), state_(std::make_unique<State>(State{std::move(economy),
        commerce::PurchaseBook(purchase_limit), QuestBook(std::move(quests), std::move(registry), event_limit)})) {
    if (!state_->economy) throw std::invalid_argument("Player requires an economy");
}
std::unique_ptr<Player::State> Player::clone_state() const {
    return std::make_unique<State>(State{state_->economy->clone(), state_->purchases, state_->quests});
}
const economy::Economy& Player::economy() const noexcept { return *state_->economy; }
const QuestBook& Player::quests() const noexcept { return state_->quests; }
QuestView Player::query(QuestId id, PlayerReadContext context) const noexcept { return state_->quests.query(id, context); }
Error Player::accept(QuestId id, PlayerReadContext context) {
    auto candidate = clone_state();
    const auto result = candidate->quests.accept(id, context);
    if (result == Error::none) state_.swap(candidate);
    return result;
}
EventResult Player::apply(const ProgressEvent& event) {
    auto candidate = clone_state();
    const auto result = candidate->quests.apply(event);
    if (result.error == Error::none && !result.replayed) state_.swap(candidate);
    return result;
}
commerce::PurchaseResult Player::purchase(const commerce::PurchaseRequest& request) {
    auto prepared = state_->purchases.prepare(products_, *state_->economy, request);
    if (prepared.result.error != commerce::Error::none || prepared.result.replayed) return std::move(prepared.result);
    auto candidate = std::make_unique<State>(State{std::move(prepared.economy), std::move(*prepared.book), state_->quests});
    state_.swap(candidate);
    return std::move(prepared.result);
}
Error Player::prepare_rewards(ClaimPreparationContext& context) {
    const auto& request = context.request;
    const auto& frozen = context.candidate.quests.quests_.at(request.quest).definition;
    const auto result = context.candidate.economy->execute({request.request_id, request.destination, {}, frozen.reward});
    if (result.error == economy::Error::request_conflict || result.replayed) return Error::request_conflict;
    if (result.error != economy::Error::none) {
        context.economy_error = result.error; context.inventory_error = result.inventory_error;
        return Error::economy_failure;
    }
    context.settlement = result.receipt;
    return Error::none;
}
Error Player::prepare_claimed(ClaimPreparationContext& context) {
    context.candidate.quests.quests_.at(context.request.quest).claimed = true;
    return Error::none;
}
Error Player::prepare_receipt(ClaimPreparationContext& context) {
    context.candidate.quests.claims_.emplace(context.request.request_id, ClaimReceipt{context.request, *context.settlement});
    return Error::none;
}
ClaimResult Player::claim(const ClaimRequest& request) {
    if (request.request_id == 0 || request.quest == 0 || request.destination == 0) return {Error::invalid_request};
    const auto previous = state_->quests.claims_.find(request.request_id);
    if (previous != state_->quests.claims_.end()) {
        if (!(previous->second.request == request)) return {Error::request_conflict};
        return {Error::none, economy::Error::none, inventory::Error::none, previous->second, true};
    }
    const auto* quest = state_->quests.accepted(request.quest);
    if (!quest) return {Error::not_accepted};
    if (quest->claimed) return {Error::already_claimed};
    if (!state_->quests.complete(*quest)) return {Error::not_ready};
    auto candidate = clone_state();
    ClaimPreparationContext context{*candidate, request};
    // Extension boundary: every stage changes ONLY this candidate. There is exactly one commit below.
    const std::array<ClaimPreparation, 3> preparations{prepare_rewards, prepare_claimed, prepare_receipt};
    for (const auto prepare : preparations) {
        const auto result = prepare(context);
        if (result != Error::none) return {result, context.economy_error, context.inventory_error};
    }
    ClaimResult output{Error::none, economy::Error::none, inventory::Error::none,
                      candidate->quests.claims_.at(request.request_id), false};
    state_.swap(candidate);
    return output;
}
void Player::replace_products(commerce::ProductCatalog products) noexcept { products_.swap(products); }
void Player::replace_quests(QuestCatalog catalog) { state_->quests.replace_catalog(std::move(catalog)); }
PlayerSnapshot Player::snapshot() const {
    return {{state_->economy->snapshot(), state_->purchases.snapshot()}, state_->quests.snapshot()};
}
Error Player::restore(const PlayerSnapshot& snapshot) {
    auto candidate = clone_state();
    if (candidate->economy->restore(snapshot.commerce.economy) != economy::Error::none ||
        candidate->purchases.restore(snapshot.commerce) != commerce::Error::none ||
        candidate->quests.restore(snapshot.quests) != Error::none) return Error::invalid_snapshot;
    std::map<economy::RequestId, const economy::Receipt*> receipts;
    std::set<economy::RequestId> purchases;
    for (const auto& receipt : snapshot.commerce.economy.receipts) receipts.emplace(receipt.transaction.request_id, &receipt);
    for (const auto& purchase : snapshot.commerce.purchases) purchases.insert(purchase.request.request_id);
    for (const auto& claim : snapshot.quests.claims) {
        const auto found = receipts.find(claim.request.request_id);
        if (found == receipts.end() || !(*found->second == claim.settlement) || purchases.count(claim.request.request_id))
            return Error::invalid_snapshot;
    }
    state_.swap(candidate);
    return Error::none;
}
} // namespace designer::quests
