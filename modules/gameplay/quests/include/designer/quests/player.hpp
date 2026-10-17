#pragma once

#include <designer/quests/quests.hpp>
#include <designer/commerce/shop.hpp>

namespace designer::quests {
struct ClaimResult {
    Error error = Error::none;
    economy::Error economy_error = economy::Error::none;
    inventory::Error inventory_error = inventory::Error::none;
    std::optional<ClaimReceipt> receipt = std::nullopt;
    bool replayed = false;
};
struct PlayerSnapshot {
    commerce::Snapshot commerce; // The ONLY economic snapshot; purchases and claims link to its receipts.
    BookSnapshot quests;
};
// Coordinates one authoritative player state. No callbacks or external effects during preparation.
class Player {
public:
    Player(commerce::ProductCatalog products, QuestCatalog quests, ObjectiveRegistry registry,
           std::unique_ptr<economy::Economy> economy, std::size_t event_limit = 256,
           std::size_t purchase_limit = 256);
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    [[nodiscard]] const economy::Economy& economy() const noexcept;
    [[nodiscard]] const QuestBook& quests() const noexcept;
    [[nodiscard]] QuestView query(QuestId id, PlayerReadContext context) const noexcept;
    [[nodiscard]] Error accept(QuestId id, PlayerReadContext context);
    [[nodiscard]] EventResult apply(const ProgressEvent& event);
    [[nodiscard]] commerce::PurchaseResult purchase(const commerce::PurchaseRequest& request);
    [[nodiscard]] ClaimResult claim(const ClaimRequest& request);
    void replace_products(commerce::ProductCatalog products) noexcept;
    void replace_quests(QuestCatalog catalog);
    [[nodiscard]] PlayerSnapshot snapshot() const;
    [[nodiscard]] Error restore(const PlayerSnapshot& snapshot);
private:
    struct State {
        std::unique_ptr<economy::Economy> economy;
        commerce::PurchaseBook purchases;
        QuestBook quests;
    };
    struct ClaimPreparationContext {
        State& candidate;
        const ClaimRequest& request;
        std::optional<economy::Receipt> settlement = std::nullopt;
        economy::Error economy_error = economy::Error::none;
        inventory::Error inventory_error = inventory::Error::none;
    };
    using ClaimPreparation = Error (*)(ClaimPreparationContext&);
    static Error prepare_rewards(ClaimPreparationContext&);
    static Error prepare_claimed(ClaimPreparationContext&);
    static Error prepare_receipt(ClaimPreparationContext&);
    [[nodiscard]] std::unique_ptr<State> clone_state() const;
    commerce::ProductCatalog products_;
    std::unique_ptr<State> state_;
};
} // namespace designer::quests
