#pragma once

#include <designer/economy/economy.hpp>
#include <string>

namespace designer::player { class Player; }

namespace designer::quests
{
    using QuestId = std::uint32_t;
    using ObjectiveId = std::uint32_t;
    using ObjectiveTypeId = std::uint32_t;
    using EventId = std::uint64_t;
    inline constexpr ObjectiveTypeId counter_type = 1, flag_type = 2;       // 预留了两种类型
    inline constexpr std::size_t max_quests = 128, max_objectives = 32, max_updates = 64, max_events = 1024;

    struct PlayerReadContext    // 任务系统读取的玩家信息
    {
        std::uint32_t level;        // 等级
        EventId event_cursor;       // 当前事件进度位置
    };

    struct ConditionSpec
    {
        std::uint32_t minimum_level;
    };

    enum class ConditionReason
    {
        none,
        level_too_low
    };

    enum class ConditionError
    {
        none,
        invalid_context,
        invalid_definition
    };

    struct ConditionResult
    {
        bool allowed = false;
        ConditionReason reason = ConditionReason::none; // 为什么不满足
        ConditionError error = ConditionError::none;
        std::uint32_t required = 0, actual = 0;         // 要求多少、实际多少
    };

    // 负责计算是否满足条件，结果把正常不满足和数据错误分开
    [[nodiscard]] ConditionResult evaluate(ConditionSpec spec, PlayerReadContext context) noexcept;

    struct ObjectiveDefinition
    {
        ObjectiveId id;
        ObjectiveTypeId type;               // 目标采用哪一种进度规则
        std::uint32_t rule_version;         // 目标规则的版本
        std::uint64_t target;               // 完成要求
    };

    inline bool operator==(const ObjectiveDefinition &a, const ObjectiveDefinition &b) noexcept
    {
        return a.id == b.id && a.type == b.type && a.rule_version == b.rule_version && a.target == b.target;
    }

    // Numeric-state v1 extension contract. Handlers are immutable, deterministic and side-effect free.
    class ObjectiveHandler
    {
    public:
        virtual ~ObjectiveHandler() = default;
        [[nodiscard]] virtual bool validate(const ObjectiveDefinition &, std::uint64_t progress) const noexcept = 0;    // 目标规则与进度是否合法
        [[nodiscard]] virtual bool update(const ObjectiveDefinition &, std::uint64_t progress,
                                          std::uint64_t input, std::uint64_t &next) const noexcept = 0;     // 根据本次输入计算新进度
        [[nodiscard]] virtual bool complete(const ObjectiveDefinition &, std::uint64_t progress) const noexcept = 0;    // 当前进度是否表示完成
    };

    class ObjectiveRegistry         // 目标类型 ID → 对应处理器
    {
    public:
        void add(ObjectiveTypeId type, std::shared_ptr<const ObjectiveHandler> handler);
        [[nodiscard]] const ObjectiveHandler *find(ObjectiveTypeId type) const noexcept;

    private:
        std::map<ObjectiveTypeId, std::shared_ptr<const ObjectiveHandler>> handlers_;
    };

    [[nodiscard]] ObjectiveRegistry default_objectives();

    // 一份任务配置
    enum class CompletionPolicy : std::uint32_t
    {
        all = 1     // 所有目标都完成，任务才完成
    };

    struct QuestDefinition
    {
        QuestId id;
        std::uint32_t version;
        std::uint32_t minimum_level;
        std::vector<ObjectiveDefinition> objectives;        // 目标
        economy::RewardBundle reward;                       // 奖励
        CompletionPolicy policy = CompletionPolicy::all;    // 完成策略
        std::string title = {};
    };

    inline bool operator==(const QuestDefinition &a, const QuestDefinition &b) noexcept
    {
        return a.id == b.id && a.version == b.version && a.minimum_level == b.minimum_level &&
               a.objectives == b.objectives && a.reward.currencies == b.reward.currencies && a.reward.items == b.reward.items &&
               a.policy == b.policy && a.title == b.title;
    }

    // 任务配置表       负责保存和查询：游戏有哪些任务、每个任务的规则是什么
    class QuestCatalog
    {
    public:
        explicit QuestCatalog(std::vector<QuestDefinition> definitions);
        [[nodiscard]] const QuestDefinition *find(QuestId id) const noexcept;
        [[nodiscard]] const std::map<QuestId, QuestDefinition> &entries() const noexcept { return definitions_; }
        void swap(QuestCatalog &other) noexcept { definitions_.swap(other.definitions_); }

    private:
        std::map<QuestId, QuestDefinition> definitions_;
    };

    enum class Error
    {
        none,
        invalid_definition,
        unknown_quest,
        already_accepted,
        condition_denied,
        invalid_context,
        unknown_objective_type,
        invalid_update,
        unknown_objective,
        not_accepted,
        already_claimed,
        not_ready,
        stale_event,
        event_before_accept,
        event_conflict,
        event_history_full,
        quest_limit,
        request_conflict,
        invalid_request,
        economy_failure,
        invalid_snapshot
    };

    [[nodiscard]] const char *error_name(Error error) noexcept;

    // 任务状态
    enum class Status
    {
        locked,             // 接取条件不满足
        available,          // 可以接取
        active,             // 已接取，目标尚未全部完成
        ready,              // 目标完成，等待领奖
        claimed             // 已经领取奖励
    };

    struct QuestView
    {
        Error error = Error::none;
        Status status = Status::locked;
        ConditionResult condition;
    };


    // 玩家实际接下的任务
    struct AcceptedQuest
    {
        QuestDefinition definition; // Frozen independently of the current catalog.
        EventId accepted_after;
        std::vector<std::uint64_t> progress; // Aligned with sorted objective IDs.
        bool claimed = false;
    };

    inline bool operator==(const AcceptedQuest &a, const AcceptedQuest &b) noexcept
    {
        return a.definition == b.definition && a.accepted_after == b.accepted_after && a.progress == b.progress && a.claimed == b.claimed;
    }

    // 给某个任务中的某个目标，提供一次进度更新输入
    struct ObjectiveUpdate
    {
        QuestId quest;
        ObjectiveId objective;
        std::uint64_t value;
    };
    inline bool operator==(const ObjectiveUpdate &a, const ObjectiveUpdate &b) noexcept
    {
        return a.quest == b.quest && a.objective == b.objective && a.value == b.value;
    }
    struct ProgressEvent
    {
        EventId id;
        std::vector<ObjectiveUpdate> updates;
    };
    inline bool operator==(const ProgressEvent &a, const ProgressEvent &b) noexcept
    {
        return a.id == b.id && a.updates == b.updates;
    }
    struct EventResult
    {
        Error error = Error::none;
        bool replayed = false;
    };


    // 用某个请求 ID，领取某任务的奖励，物品放入指定容器
    struct ClaimRequest
    {
        economy::RequestId request_id;
        QuestId quest;
        economy::ContainerId destination;
    };
    inline bool operator==(const ClaimRequest &a, const ClaimRequest &b) noexcept
    {
        return a.request_id == b.request_id && a.quest == b.quest && a.destination == b.destination;
    }

    // 领了哪些任务，实际发放了哪些资产
    struct ClaimReceipt
    {
        ClaimRequest request;
        economy::Receipt settlement;
    };
    inline bool operator==(const ClaimReceipt &a, const ClaimReceipt &b) noexcept
    {
        return a.request == b.request && a.settlement == b.settlement;
    }


    struct BookSnapshot
    {
        std::vector<AcceptedQuest> quests; // 已接任务
        std::vector<ProgressEvent> events; // 事件历史
        std::vector<ClaimReceipt> claims;  // 领奖凭证
    };

    inline bool operator==(const BookSnapshot &a, const BookSnapshot &b) noexcept
    {
        return a.quests == b.quests && a.events == b.events && a.claims == b.claims;
    }


    // Engine-independent task state. No commerce dependency, no external effects.
    class QuestBook
    {
    public:
        QuestBook(QuestCatalog catalog, ObjectiveRegistry registry, std::size_t event_limit = 256);

        [[nodiscard]] QuestView query(QuestId id, PlayerReadContext context) const noexcept;

        [[nodiscard]] Error accept(QuestId id, PlayerReadContext context);

        [[nodiscard]] EventResult apply(const ProgressEvent &event);

        [[nodiscard]] const AcceptedQuest *accepted(QuestId id) const noexcept;

        void replace_catalog(QuestCatalog catalog);

        [[nodiscard]] BookSnapshot snapshot() const;

        [[nodiscard]] Error restore(const BookSnapshot &snapshot);

    private:
        friend class designer::player::Player;

        [[nodiscard]] Error validate(const QuestDefinition &definition) const;

        [[nodiscard]] bool complete(const AcceptedQuest &quest) const noexcept;


        QuestCatalog catalog_;
        ObjectiveRegistry registry_;
        std::size_t event_limit_;

        std::map<QuestId, AcceptedQuest> quests_;               // 已接任务
        std::map<EventId, ProgressEvent> events_;               // 已成功处理的进度事件
        std::map<economy::RequestId, ClaimReceipt> claims_;     // 成功领奖凭证
    };
} // namespace designer::quests
