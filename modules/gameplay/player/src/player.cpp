#include <utility>
#include "dialogue_links.hpp"
#include <designer/player/player.hpp>
#include <array>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace designer::player
{
    static_assert(std::is_nothrow_move_constructible<ClaimResult>::value, "Claim return must not allocate after commit");
    Player::Player(quests::QuestCatalog quests, quests::ObjectiveRegistry registry,
                   std::unique_ptr<economy::Economy> economy, std::size_t event_limit, std::size_t purchase_limit, interaction::Catalog interactions, std::size_t interaction_limit, dialogue::Catalog dialogues, std::size_t dialogue_limit)
        : interactions_(std::move(interactions)), dialogues_(std::move(dialogues)), state_(std::make_unique<State>(State{std::move(economy),
                                                                               commerce::PurchaseBook(purchase_limit), quests::QuestBook(std::move(quests), std::move(registry), event_limit), interaction::Book(interaction_limit), dialogue::Book(dialogue_limit)}))
    {
        if (!state_->economy)
            throw std::invalid_argument("Player requires an economy");
    }

    std::unique_ptr<Player::State> Player::clone_state() const
    {
        return std::make_unique<State>(State{state_->economy->clone(), state_->purchases, state_->quests, state_->interactions, state_->dialogues});
    }

    const economy::Economy &Player::economy() const noexcept { return *state_->economy; }

    const quests::QuestBook &Player::quests() const noexcept { return state_->quests; }

    quests::QuestView Player::query_quest_status(quests::QuestId id, quests::PlayerReadContext context) const noexcept { return state_->quests.query_quest_status(id, context); }

    quests::Error Player::accept_quest(quests::QuestId id, quests::PlayerReadContext context)
    {
        auto candidate = clone_state();
        const auto result = candidate->quests.accept_quest(id, context);
        if (result == quests::Error::none)
            state_.swap(candidate);
        return result;
    }

    quests::EventResult Player::apply_quest_progress_event(const quests::ProgressEvent &event)
    {
        auto candidate = clone_state();
        const auto result = candidate->quests.apply_progress_event(event);
        if (result.error == quests::Error::none && !result.replayed)
            state_.swap(candidate);
        return result;
    }

    quests::EventResult Player::observe_gameplay_fact(const quests::GameplayFact &fact)
    {
        auto candidate = clone_state();
        const auto result = candidate->quests.observe_gameplay_fact(fact);
        if (result.error == quests::Error::none && !result.replayed)
            state_.swap(candidate);
        return result;
    }

    commerce::PurchaseResult Player::purchase_product(const commerce::ProductCatalog &products,
                                                      const commerce::PurchaseRequest &request)
    {
        auto prepared = state_->purchases.prepare(products, *state_->economy, request);
        if (prepared.result.error != commerce::Error::none || prepared.result.replayed)
            return std::move(prepared.result);
        auto candidate = std::make_unique<State>(State{std::move(prepared.economy), std::move(*prepared.book), state_->quests, state_->interactions, state_->dialogues});
        state_.swap(candidate);
        return std::move(prepared.result);
    }

    const interaction::Book& Player::interactions() const noexcept { return state_->interactions; }
    interaction::View Player::check_interaction_availability(interaction::PointId point) const noexcept {
        auto view = state_->interactions.check_interaction_availability(interactions_, point);
        if (view.allowed && state_->quests.event_cursor() == std::numeric_limits<quests::EventId>::max())
            return {false, interaction::Error::event_id_exhausted};
        return view;
    }
    void Player::replace_interactions(interaction::Catalog catalog) noexcept { interactions_.swap(catalog); }
    InteractionResult Player::execute_interaction(const interaction::Request& request) {
        if (request.request_id == 0 || request.point == 0) return {interaction::Error::invalid_request};
        if (const auto* prior = state_->interactions.find_interaction_receipt(request.request_id)) {
            if (!(prior->request == request)) return {interaction::Error::request_conflict};
            return {interaction::Error::none, quests::Error::none, *prior, true};
        }
        const auto view = check_interaction_availability(request.point);
        if (!view.allowed) return {view.reason};
        const auto event = state_->quests.event_cursor() + 1; // check_interaction_availability 已检查溢出；包括已知接取边界。
        auto candidate = clone_state();
        const auto result = candidate->interactions.execute_interaction(interactions_, request, event);
        if (result.error != interaction::Error::none) return {result.error};
        const auto& receipt = *result.receipt;
        const auto delivered = candidate->quests.observe_gameplay_fact({receipt.event_id, receipt.fact_type, request.point, 1});
        if (delivered.error != quests::Error::none || delivered.replayed)
            return {interaction::Error::fact_rejected, delivered.replayed ? quests::Error::event_conflict : delivered.error};
        InteractionResult output{interaction::Error::none, quests::Error::none, receipt, false};
        static_assert(std::is_nothrow_move_constructible<InteractionResult>::value, "Return must not allocate after commit");
        state_.swap(candidate);
        return output;
    }

    const dialogue::Book& Player::dialogues() const noexcept { return state_->dialogues; }
    void Player::replace_dialogues(dialogue::Catalog catalog) noexcept { dialogues_.swap(catalog); }
    DialogueResult Player::execute_dialogue_command(const dialogue::Command& command) {
        if (!dialogue::valid_dialogue_command(command)) return {dialogue::Error::invalid_command};
        if (const auto* prior = state_->dialogues.find_dialogue_receipt(command.request_id)) {
            if (!(prior->command == command)) return {dialogue::Error::request_conflict};
            return {dialogue::Error::none, quests::Error::none, *prior, true};
        }
        quests::EventId event = 0;
        if (command.action == dialogue::Action::finish) {
            if (state_->quests.event_cursor() == std::numeric_limits<quests::EventId>::max())
                return {dialogue::Error::event_id_exhausted};
            event = state_->quests.event_cursor() + 1;
        }
        auto candidate = clone_state();
        const auto result = candidate->dialogues.execute_dialogue_command(dialogues_, command, event);
        if (result.error != dialogue::Error::none) return {result.error};
        if (event) {
            const auto view = candidate->dialogues.query_dialogue_session(result.receipt->session);
            const auto delivered = candidate->quests.observe_gameplay_fact({event, view->definition.completion_fact_type, view->definition.id, 1});
            if (delivered.error != quests::Error::none || delivered.replayed)
                return {dialogue::Error::fact_rejected, delivered.replayed ? quests::Error::event_conflict : delivered.error};
        }
        DialogueResult output{dialogue::Error::none, quests::Error::none, result.receipt, false};
        static_assert(std::is_nothrow_move_constructible<DialogueResult>::value, "Return must not allocate after commit");
        state_.swap(candidate);
        return output;
    }

    quests::Error Player::prepare_rewards(ClaimPreparationContext &context)
    {
        const auto &request = context.request;
        const auto &frozen = context.candidate.quests.quests_.at(request.quest).definition;
        const auto result = context.candidate.economy->execute({request.request_id, request.destination, {}, frozen.reward});
        if (result.error == economy::Error::request_conflict || result.replayed)
            return quests::Error::request_conflict;
        if (result.error != economy::Error::none)
        {
            context.economy_error = result.error;
            context.inventory_error = result.inventory_error;
            return quests::Error::economy_failure;
        }
        context.settlement = result.receipt;
        return quests::Error::none;
    }

    quests::Error Player::prepare_claimed(ClaimPreparationContext &context)     // 将任务标记为已领取
    {
        auto& book = context.candidate.quests;
        auto& quest = book.quests_.at(context.request.quest);
        quest.claimed = true;
        for (const auto& objective : quest.definition.objectives)
            if (objective.binding) {
                quest.fact_until = book.events_.empty() ? 0 : book.events_.rbegin()->first;
                break;
            }
        return quests::Error::none;
    }

    quests::Error Player::prepare_receipt(ClaimPreparationContext &context)     // 保存领奖凭证
    {
        context.candidate.quests.claims_.emplace(context.request.request_id, quests::ClaimReceipt{context.request, *context.settlement});
        return quests::Error::none;
    }

    ClaimResult Player::claim_quest_reward(const quests::ClaimRequest &request)
    {
        if (request.request_id == 0 || request.quest == 0 || request.destination == 0)
            return {quests::Error::invalid_request};

        const auto previous = state_->quests.claims_.find(request.request_id);
        if (previous != state_->quests.claims_.end())
        {
            if (!(previous->second.request == request))
                return {quests::Error::request_conflict};
            return {quests::Error::none, economy::Error::none, inventory::Error::none, previous->second, true};
        }

        const auto *quest = state_->quests.find_accepted_quest(request.quest);
        if (!quest)
            return {quests::Error::not_accepted};
        if (quest->claimed)
            return {quests::Error::already_claimed};
        if (!state_->quests.complete(*quest))
            return {quests::Error::not_ready};

        auto candidate = clone_state();
        ClaimPreparationContext context{*candidate, request};
        // Extension boundary: every stage changes ONLY this candidate. There is exactly one commit below.
        const std::array<ClaimPreparation, 3> preparations{prepare_rewards, prepare_claimed, prepare_receipt};
        for (const auto prepare : preparations)
        {
            const auto result = prepare(context);
            if (result != quests::Error::none)
                return {result, context.economy_error, context.inventory_error};
        }
        ClaimResult output{quests::Error::none, economy::Error::none, inventory::Error::none,
                           candidate->quests.claims_.at(request.request_id), false};
        state_.swap(candidate);
        return output;
    }

    void Player::replace_quests(quests::QuestCatalog catalog) { state_->quests.replace_catalog(std::move(catalog)); }

    PlayerSnapshot Player::snapshot() const
    {
        return {{state_->economy->snapshot(), state_->purchases.snapshot()}, state_->quests.snapshot(), state_->interactions.snapshot(), state_->dialogues.snapshot()};
    }

    quests::Error Player::restore(const PlayerSnapshot &snapshot)
    {
        auto candidate = clone_state();
        if (candidate->economy->restore(snapshot.commerce.economy) != economy::Error::none ||
            candidate->purchases.restore(snapshot.commerce) != commerce::Error::none ||
            candidate->quests.restore(snapshot.quests) != quests::Error::none ||
            candidate->interactions.restore(snapshot.interactions) != interaction::Error::none ||
            candidate->dialogues.restore(snapshot.dialogues) != dialogue::Error::none || !detail::valid_dialogue_links(snapshot))
            return quests::Error::invalid_snapshot;
        std::map<economy::RequestId, const economy::Receipt *> receipts;
        std::set<economy::RequestId> purchases;
        for (const auto &receipt : snapshot.commerce.economy.receipts)
            receipts.emplace(receipt.transaction.request_id, &receipt);
        for (const auto &purchase : snapshot.commerce.purchases)
            purchases.insert(purchase.request.request_id);
        for (const auto &claim : snapshot.quests.claims)
        {
            const auto found = receipts.find(claim.request.request_id);
            if (found == receipts.end() || !(*found->second == claim.settlement) || purchases.count(claim.request.request_id))
                return quests::Error::invalid_snapshot;
        }
        for (const auto& receipt : snapshot.interactions) {
            const auto found = std::lower_bound(snapshot.quests.events.begin(), snapshot.quests.events.end(), receipt.event_id,
                [](const auto& event, quests::EventId id) { return event.id < id; });
            const quests::GameplayFact expected{receipt.event_id, receipt.fact_type, receipt.request.point, 1};
            if (found == snapshot.quests.events.end() || found->id != receipt.event_id || !found->fact || !(*found->fact == expected))
                return quests::Error::invalid_snapshot;
        }
        state_.swap(candidate);
        return quests::Error::none;
    }
} // namespace designer::player
