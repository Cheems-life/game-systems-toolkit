#pragma once

#include <designer/quests/quests.hpp>
#include <designer/commerce/shop.hpp>
#include <designer/interaction/interaction.hpp>
#include <designer/dialogue/dialogue.hpp>

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

    struct InteractionResult {
        interaction::Error error = interaction::Error::none;
        quests::Error quest_error = quests::Error::none;
        std::optional<interaction::Receipt> receipt = std::nullopt;
        bool replayed = false;
    };

    struct DialogueResult {
        dialogue::Error error = dialogue::Error::none;
        quests::Error quest_error = quests::Error::none;
        std::optional<dialogue::Receipt> receipt = std::nullopt;
        bool replayed = false;
    };

    struct PlayerSnapshot
    {
        commerce::Snapshot commerce; // The ONLY economic snapshot; purchases and claims link to its receipts.
        quests::BookSnapshot quests;
        interaction::Snapshot interactions = {};
        dialogue::Snapshot dialogues = {};
    };

    // Coordinates one authoritative player state. No callbacks or external effects during preparation.
    class Player
    {
    public:
        Player(quests::QuestCatalog quests, quests::ObjectiveRegistry registry,
               std::unique_ptr<economy::Economy> assets, std::size_t event_limit = 256,
               std::size_t purchase_limit = 256, interaction::Catalog interactions = interaction::Catalog({}),
               std::size_t interaction_limit = 256, dialogue::Catalog dialogues = dialogue::Catalog({}),
               std::size_t dialogue_limit = 256);

        Player(const Player &) = delete;
        Player &operator=(const Player &) = delete;

        [[nodiscard]] const economy::Economy &economy() const noexcept;

        [[nodiscard]] const quests::QuestBook &quests() const noexcept;

        [[nodiscard]] quests::QuestView query_quest_status(quests::QuestId id, quests::PlayerReadContext context) const noexcept;        // 查看任务现在是什么状态

        [[nodiscard]] quests::Error accept_quest(quests::QuestId id, quests::PlayerReadContext context);          // 正式接取一个任务

        [[nodiscard]] quests::EventResult apply_quest_progress_event(const quests::ProgressEvent &event);                        // 提交事件，推进任务目标

        [[nodiscard]] quests::EventResult observe_gameplay_fact(const quests::GameplayFact &fact); // 匹配冻结绑定并统一推进

        // 只在本次调用借用可信商品目录，不保存引用或副本。
        [[nodiscard]] commerce::PurchaseResult purchase_product(const commerce::ProductCatalog &products,
                                                               const commerce::PurchaseRequest &request); // 购买商品，统一扣款与发奖

        [[nodiscard]] interaction::View check_interaction_availability(interaction::PointId point) const noexcept;
        [[nodiscard]] InteractionResult execute_interaction(const interaction::Request& request);
        [[nodiscard]] const interaction::Book& interactions() const noexcept;
        void replace_interactions(interaction::Catalog catalog) noexcept;

        [[nodiscard]] const dialogue::Book& dialogues() const noexcept;
        [[nodiscard]] DialogueResult execute_dialogue_command(const dialogue::Command& command);
        void replace_dialogues(dialogue::Catalog catalog) noexcept;

        [[nodiscard]] ClaimResult claim_quest_reward(const quests::ClaimRequest &request);

        void replace_quests(quests::QuestCatalog catalog);

        [[nodiscard]] PlayerSnapshot snapshot() const;

        [[nodiscard]] quests::Error restore(const PlayerSnapshot &snapshot);

    private:
        struct State    // 玩家业务状态
        {
            std::unique_ptr<economy::Economy> assets; // 本玩家的钱包、物品容器与经济交易记录
            commerce::PurchaseBook purchases;
            quests::QuestBook quests;
            interaction::Book interactions;
            dialogue::Book dialogues;
        };

        struct ClaimPreparationContext
        {
            State &candidate;                                               // 本次操作的候选玩家状态
            const quests::ClaimRequest &request;                            // 本次领取哪个任务、奖励放到哪里
            std::optional<economy::Receipt> settlement = std::nullopt;      // 发奖成功后产生的经济结算凭证
            economy::Error economy_error = economy::Error::none;            // 发奖失败时的经济层错误
            inventory::Error inventory_error = inventory::Error::none;      // 背包错误
        };

        using ClaimPreparation = quests::Error (*)(ClaimPreparationContext &);

        static quests::Error prepare_rewards(ClaimPreparationContext &);
        static quests::Error prepare_claimed(ClaimPreparationContext &);
        static quests::Error prepare_receipt(ClaimPreparationContext &);

        [[nodiscard]] std::unique_ptr<State> clone_state() const;

        interaction::Catalog interactions_;
        dialogue::Catalog dialogues_;
        std::unique_ptr<State> state_;
    };
} // namespace designer::player
