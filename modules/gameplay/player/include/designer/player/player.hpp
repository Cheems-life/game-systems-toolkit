#pragma once

#include <designer/quests/quests.hpp>
#include <designer/commerce/shop.hpp>

// 把任务、商城购买和经济资产组织在一起，保证跨系统操作保持一致

namespace designer::player
{
    struct ClaimResult
    {
        quests::Error error = quests::Error::none;
        economy::Error economy_error = economy::Error::none;
        inventory::Error inventory_error = inventory::Error::none;
        std::optional<quests::ClaimReceipt> receipt = std::nullopt;
        bool replayed = false;
    };

    struct PlayerSnapshot
    {
        commerce::Snapshot commerce; // The ONLY economic snapshot; purchases and claims link to its receipts.
        quests::BookSnapshot quests;
    };

    // Coordinates one authoritative player state. No callbacks or external effects during preparation.
    class Player
    {
    public:
        Player(commerce::ProductCatalog products, quests::QuestCatalog quests, quests::ObjectiveRegistry registry,
               std::unique_ptr<economy::Economy> economy, std::size_t event_limit = 256,
               std::size_t purchase_limit = 256);

        Player(const Player &) = delete;
        Player &operator=(const Player &) = delete;

        [[nodiscard]] const economy::Economy &economy() const noexcept;

        [[nodiscard]] const quests::QuestBook &quests() const noexcept;

        [[nodiscard]] quests::QuestView query(quests::QuestId id, quests::PlayerReadContext context) const noexcept;

        [[nodiscard]] quests::Error accept(quests::QuestId id, quests::PlayerReadContext context);

        [[nodiscard]] quests::EventResult apply(const quests::ProgressEvent &event);

        [[nodiscard]] quests::EventResult observe(const quests::GameplayFact &fact); // 匹配冻结绑定并统一推进

        [[nodiscard]] commerce::PurchaseResult purchase(const commerce::PurchaseRequest &request);

        [[nodiscard]] ClaimResult claim(const quests::ClaimRequest &request);

        void replace_products(commerce::ProductCatalog products) noexcept;

        void replace_quests(quests::QuestCatalog catalog);

        [[nodiscard]] PlayerSnapshot snapshot() const;

        [[nodiscard]] quests::Error restore(const PlayerSnapshot &snapshot);

    private:
        struct State
        {
            std::unique_ptr<economy::Economy> economy;
            commerce::PurchaseBook purchases;
            quests::QuestBook quests;
        };

        struct ClaimPreparationContext
        {
            State &candidate;
            const quests::ClaimRequest &request;
            std::optional<economy::Receipt> settlement = std::nullopt;
            economy::Error economy_error = economy::Error::none;
            inventory::Error inventory_error = inventory::Error::none;
        };

        using ClaimPreparation = quests::Error (*)(ClaimPreparationContext &);

        static quests::Error prepare_rewards(ClaimPreparationContext &);
        static quests::Error prepare_claimed(ClaimPreparationContext &);
        static quests::Error prepare_receipt(ClaimPreparationContext &);

        [[nodiscard]] std::unique_ptr<State> clone_state() const;

        commerce::ProductCatalog products_;
        std::unique_ptr<State> state_;
    };
} // namespace designer::player
