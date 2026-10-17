#pragma once

#include <designer/economy/economy.hpp>
#include <string>

namespace designer::quests {
using QuestId = std::uint32_t;
using ObjectiveId = std::uint32_t;
using ObjectiveTypeId = std::uint32_t;
using EventId = std::uint64_t;
inline constexpr ObjectiveTypeId counter_type = 1, flag_type = 2;
inline constexpr std::size_t max_quests = 128, max_objectives = 32, max_updates = 64, max_events = 1024;

struct PlayerReadContext { std::uint32_t level; EventId event_cursor; };
struct ConditionSpec { std::uint32_t minimum_level; };
enum class ConditionReason { none, level_too_low };
enum class ConditionError { none, invalid_context, invalid_definition };
struct ConditionResult {
    bool allowed = false;
    ConditionReason reason = ConditionReason::none;
    ConditionError error = ConditionError::none;
    std::uint32_t required = 0, actual = 0;
};
[[nodiscard]] ConditionResult evaluate(ConditionSpec spec, PlayerReadContext context) noexcept;

struct ObjectiveDefinition {
    ObjectiveId id;
    ObjectiveTypeId type;
    std::uint32_t rule_version;
    std::uint64_t target;
};
inline bool operator==(const ObjectiveDefinition& a, const ObjectiveDefinition& b) noexcept {
    return a.id == b.id && a.type == b.type && a.rule_version == b.rule_version && a.target == b.target;
}
// Numeric-state v1 extension contract. Handlers are immutable, deterministic and side-effect free.
class ObjectiveHandler {
public:
    virtual ~ObjectiveHandler() = default;
    [[nodiscard]] virtual bool validate(const ObjectiveDefinition&, std::uint64_t progress) const noexcept = 0;
    [[nodiscard]] virtual bool update(const ObjectiveDefinition&, std::uint64_t progress,
                                    std::uint64_t input, std::uint64_t& next) const noexcept = 0;
    [[nodiscard]] virtual bool complete(const ObjectiveDefinition&, std::uint64_t progress) const noexcept = 0;
};
class ObjectiveRegistry {
public:
    void add(ObjectiveTypeId type, std::shared_ptr<const ObjectiveHandler> handler);
    [[nodiscard]] const ObjectiveHandler* find(ObjectiveTypeId type) const noexcept;
private:
    std::map<ObjectiveTypeId, std::shared_ptr<const ObjectiveHandler>> handlers_;
};
[[nodiscard]] ObjectiveRegistry default_objectives();
enum class CompletionPolicy : std::uint32_t { all = 1 };
struct QuestDefinition {
    QuestId id;
    std::uint32_t version;
    std::uint32_t minimum_level;
    std::vector<ObjectiveDefinition> objectives;
    economy::RewardBundle reward;
    CompletionPolicy policy = CompletionPolicy::all;
    std::string title = {};
};
inline bool operator==(const QuestDefinition& a, const QuestDefinition& b) noexcept {
    return a.id == b.id && a.version == b.version && a.minimum_level == b.minimum_level &&
        a.objectives == b.objectives && a.reward.currencies == b.reward.currencies && a.reward.items == b.reward.items &&
        a.policy == b.policy && a.title == b.title;
}
class QuestCatalog {
public:
    explicit QuestCatalog(std::vector<QuestDefinition> definitions);
    [[nodiscard]] const QuestDefinition* find(QuestId id) const noexcept;
    [[nodiscard]] const std::map<QuestId, QuestDefinition>& entries() const noexcept { return definitions_; }
    void swap(QuestCatalog& other) noexcept { definitions_.swap(other.definitions_); }
private:
    std::map<QuestId, QuestDefinition> definitions_;
};
enum class Error {
    none, invalid_definition, unknown_quest, already_accepted, condition_denied, invalid_context,
    unknown_objective_type, invalid_update, unknown_objective, not_accepted, already_claimed,
    not_ready, stale_event, event_before_accept, event_conflict, event_history_full,
    quest_limit, request_conflict, invalid_request, economy_failure, invalid_snapshot
};
[[nodiscard]] const char* error_name(Error error) noexcept;
enum class Status { locked, available, active, ready, claimed };
struct QuestView {
    Error error = Error::none;
    Status status = Status::locked;
    ConditionResult condition;
};
struct AcceptedQuest {
    QuestDefinition definition; // Frozen independently of the current catalog.
    EventId accepted_after;
    std::vector<std::uint64_t> progress; // Aligned with sorted objective IDs.
    bool claimed = false;
};
inline bool operator==(const AcceptedQuest& a, const AcceptedQuest& b) noexcept {
    return a.definition == b.definition && a.accepted_after == b.accepted_after && a.progress == b.progress && a.claimed == b.claimed;
}
struct ObjectiveUpdate { QuestId quest; ObjectiveId objective; std::uint64_t value; };
inline bool operator==(const ObjectiveUpdate& a, const ObjectiveUpdate& b) noexcept {
    return a.quest == b.quest && a.objective == b.objective && a.value == b.value;
}
struct ProgressEvent { EventId id; std::vector<ObjectiveUpdate> updates; };
inline bool operator==(const ProgressEvent& a, const ProgressEvent& b) noexcept {
    return a.id == b.id && a.updates == b.updates;
}
struct EventResult { Error error = Error::none; bool replayed = false; };
struct ClaimRequest { economy::RequestId request_id; QuestId quest; economy::ContainerId destination; };
inline bool operator==(const ClaimRequest& a, const ClaimRequest& b) noexcept {
    return a.request_id == b.request_id && a.quest == b.quest && a.destination == b.destination;
}
struct ClaimReceipt { ClaimRequest request; economy::Receipt settlement; };
inline bool operator==(const ClaimReceipt& a, const ClaimReceipt& b) noexcept {
    return a.request == b.request && a.settlement == b.settlement;
}
struct BookSnapshot {
    std::vector<AcceptedQuest> quests; // Increasing quest ID.
    std::vector<ProgressEvent> events; // Increasing event ID, no eviction.
    std::vector<ClaimReceipt> claims; // Increasing economic sequence.
};
inline bool operator==(const BookSnapshot& a, const BookSnapshot& b) noexcept {
    return a.quests == b.quests && a.events == b.events && a.claims == b.claims;
}
class Player;
// Engine-independent task state. No commerce dependency, no external effects.
class QuestBook {
public:
    QuestBook(QuestCatalog catalog, ObjectiveRegistry registry, std::size_t event_limit = 256);
    [[nodiscard]] QuestView query(QuestId id, PlayerReadContext context) const noexcept;
    [[nodiscard]] Error accept(QuestId id, PlayerReadContext context);
    [[nodiscard]] EventResult apply(const ProgressEvent& event);
    [[nodiscard]] const AcceptedQuest* accepted(QuestId id) const noexcept;
    void replace_catalog(QuestCatalog catalog);
    [[nodiscard]] BookSnapshot snapshot() const;
    [[nodiscard]] Error restore(const BookSnapshot& snapshot);
private:
    friend class Player;
    [[nodiscard]] Error validate(const QuestDefinition& definition) const;
    [[nodiscard]] bool complete(const AcceptedQuest& quest) const noexcept;
    QuestCatalog catalog_;
    ObjectiveRegistry registry_;
    std::size_t event_limit_;
    std::map<QuestId, AcceptedQuest> quests_;
    std::map<EventId, ProgressEvent> events_;
    std::map<economy::RequestId, ClaimReceipt> claims_;
};
} // namespace designer::quests
