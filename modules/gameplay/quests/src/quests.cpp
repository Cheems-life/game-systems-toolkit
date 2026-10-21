#include "fact_routing.hpp"
#include <utility>
#include <designer/quests/quests.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace designer::quests
{
    namespace
    {
        bool update_less(const ObjectiveUpdate &a, const ObjectiveUpdate &b) noexcept
        {
            return a.quest < b.quest || (a.quest == b.quest && a.objective < b.objective);
        }

        // 整理并校验事件
        Error normalize_event(ProgressEvent &event)
        {
            if (event.id == 0 || event.updates.size() > max_updates ||
                (event.fact && (!valid_fact(*event.fact) || event.fact->id != event.id)))
                return Error::invalid_update;
            std::sort(event.updates.begin(), event.updates.end(), update_less);
            for (std::size_t i = 0; i < event.updates.size(); ++i)
            {
                const auto &u = event.updates[i];
                if (u.quest == 0 || u.objective == 0 || u.value == 0 || (i > 0 && !update_less(event.updates[i - 1], u)))
                    return Error::invalid_update;
            }
            return Error::none;
        }

        // 推进一个已接任务的一个目标
        Error advance(AcceptedQuest &quest, const ObjectiveUpdate &update, EventId event, const ObjectiveRegistry &registry)
        {
            if (event <= quest.accepted_after)
                return Error::event_before_accept;
            const auto &definitions = quest.definition.objectives;
            auto found = std::lower_bound(definitions.begin(), definitions.end(), update.objective,
                                          [](const auto &d, ObjectiveId id)
                                          { return d.id < id; });
            if (found == definitions.end() || found->id != update.objective)
                return Error::unknown_objective;

            const auto *handler = registry.find(found->type);       // 根据目标类型取得处理器
            if (!handler)
                return Error::unknown_objective_type;
            auto &progress = quest.progress[static_cast<std::size_t>(found - definitions.begin())];
            std::uint64_t next = progress;
            if (!handler->update(*found, progress, update.value, next) || !handler->validate(*found, next) || next < progress || next > found->target)
                return Error::invalid_update;
            progress = next;
            return Error::none;
        }

    } // namespace
    const char *error_name(Error e) noexcept
    {
        switch (e)
        {
#define QUEST_ERROR(name) \
    case Error::name:     \
        return #name
            QUEST_ERROR(none);
            QUEST_ERROR(invalid_definition);
            QUEST_ERROR(unknown_quest);
            QUEST_ERROR(already_accepted);
            QUEST_ERROR(condition_denied);
            QUEST_ERROR(invalid_context);
            QUEST_ERROR(unknown_objective_type);
            QUEST_ERROR(invalid_update);
            QUEST_ERROR(unknown_objective);
            QUEST_ERROR(not_accepted);
            QUEST_ERROR(already_claimed);
            QUEST_ERROR(not_ready);
            QUEST_ERROR(stale_event);
            QUEST_ERROR(event_before_accept);
            QUEST_ERROR(event_conflict);
            QUEST_ERROR(event_history_full);
            QUEST_ERROR(quest_limit);
            QUEST_ERROR(request_conflict);
            QUEST_ERROR(invalid_request);
            QUEST_ERROR(economy_failure);
            QUEST_ERROR(invalid_snapshot);
#undef QUEST_ERROR
        }
        return "unrecognized_error";
    }

    QuestBook::QuestBook(QuestCatalog catalog, ObjectiveRegistry registry, std::size_t event_limit)
        : catalog_(std::move(catalog)), registry_(std::move(registry)), event_limit_(event_limit)
    {
        if (event_limit == 0 || event_limit > max_events)
            throw std::invalid_argument("Invalid quest event limit");
        for (const auto &entry : catalog_.entries())
            if (validate(entry.second) != Error::none)
                throw std::invalid_argument("Unsupported quest objective");
    }

    Error QuestBook::validate(const QuestDefinition &definition) const
    {
        try
        {
            QuestCatalog structural({definition});
            if (!(*structural.find(definition.id) == definition))
                return Error::invalid_definition;
        }
        catch (const std::invalid_argument &)
        {
            return Error::invalid_definition;
        }
        for (const auto &objective : definition.objectives)
        {
            const auto *handler = registry_.find(objective.type);
            if (!handler)
                return Error::unknown_objective_type;
            if (!handler->validate(objective, 0))
                return Error::invalid_definition;
        }
        return Error::none;
    }

    // 判断一个已接任务是否完成
    bool QuestBook::complete(const AcceptedQuest &quest) const noexcept
    {
        // Composition boundary: v1 supports ALL; leaf handlers do not decide grouping or activation order.
        if (quest.definition.policy != CompletionPolicy::all)
            return false;
        for (std::size_t i = 0; i < quest.progress.size(); ++i)
        {
            const auto &definition = quest.definition.objectives[i];
            const auto *handler = registry_.find(definition.type);
            if (!handler || !handler->complete(definition, quest.progress[i]))
                return false;
        }
        return true;
    }

    const AcceptedQuest *QuestBook::accepted(QuestId id) const noexcept
    {
        const auto found = quests_.find(id);
        return found == quests_.end() ? nullptr : &found->second;
    }

    QuestView QuestBook::query(QuestId id, PlayerReadContext context) const noexcept
    {
        if (const auto *quest = accepted(id))
            return {Error::none, quest->claimed ? Status::claimed : complete(*quest) ? Status::ready
                                                                                     : Status::active,
                    {true}};
        const auto *definition = catalog_.find(id);
        if (!definition)
            return {Error::unknown_quest, Status::locked, {}};
        const auto result = evaluate({definition->minimum_level}, context);
        return {result.error == ConditionError::none ? Error::none : Error::invalid_context,
                result.allowed ? Status::available : Status::locked, result};
    }

    EventId QuestBook::event_cursor() const noexcept {
        EventId cursor = events_.empty() ? 0 : events_.rbegin()->first;
        for (const auto& entry : quests_) cursor = std::max(cursor, entry.second.accepted_after);
        return cursor;
    }

    Error QuestBook::accept(QuestId id, PlayerReadContext context)
    {
        if (accepted(id))
            return Error::already_accepted;

        const auto view = query(id, context);       // 查询任务与接取条件
        if (view.error != Error::none)
            return view.error;

        if (!view.condition.allowed)
            return Error::condition_denied;

        const auto known_boundary = event_cursor();
        if (context.event_cursor < known_boundary)
            return Error::invalid_context;
        if (quests_.size() >= max_quests)
            return Error::quest_limit;
        const auto &definition = *catalog_.find(id);

        AcceptedQuest candidate{definition, context.event_cursor, std::vector<std::uint64_t>(definition.objectives.size(), 0), false};
        quests_.emplace(id, std::move(candidate));

        return Error::none;
    }

    void QuestBook::replace_catalog(QuestCatalog catalog)
    {
        for (const auto &entry : catalog.entries())
            if (validate(entry.second) != Error::none)
                throw std::invalid_argument("Unsupported quest objective");
        catalog_.swap(catalog);
    }

    EventResult QuestBook::apply(const ProgressEvent &input)
    {
        if (input.updates.size() > max_updates)
            return {Error::invalid_update};

        auto event = input;
        const auto normalized = normalize_event(event);
        if (normalized != Error::none)
            return {normalized};

        const auto previous = events_.find(event.id);
        if (previous != events_.end())
            return previous->second == event ? EventResult{Error::none, true} : EventResult{Error::event_conflict};
        if (!events_.empty() && event.id <= events_.rbegin()->first)
            return {Error::stale_event};
        if (events_.size() >= event_limit_)
            return {Error::event_history_full};

        if (event.fact) {
            std::vector<ObjectiveUpdate> expected;
            if (detail::route_fact(*event.fact, quests_, false, expected) != Error::none || expected != event.updates)
                return {Error::invalid_update};
        }

        // 复制候选状态
        auto candidate_quests = quests_;
        auto candidate_events = events_;

        for (const auto &update : event.updates)
        {
            const auto found = candidate_quests.find(update.quest);
            if (found == candidate_quests.end())
                return {Error::not_accepted};
            if (found->second.claimed)
                return {Error::already_claimed};
            const auto result = advance(found->second, update, event.id, registry_);
            if (result != Error::none)
                return {result};
        }

        candidate_events.emplace(event.id, std::move(event));
        quests_.swap(candidate_quests);
        events_.swap(candidate_events);
        return {};
    }

    BookSnapshot QuestBook::snapshot() const
    {
        BookSnapshot result;
        for (const auto &entry : quests_)
            result.quests.push_back(entry.second);
        for (const auto &entry : events_)
            result.events.push_back(entry.second);
        for (const auto &entry : claims_)
            result.claims.push_back(entry.second);
        std::sort(result.claims.begin(), result.claims.end(), [](const auto &a, const auto &b)
                  { return a.settlement.sequence < b.settlement.sequence; });
        return result;
    }

    Error QuestBook::restore(const BookSnapshot &snapshot)
    {
        if (snapshot.quests.size() > max_quests || snapshot.events.size() > event_limit_ || snapshot.claims.size() > max_quests)
            return Error::invalid_snapshot;

        // 创建三个临时映射
        std::map<QuestId, AcceptedQuest> quests;
        std::map<EventId, ProgressEvent> events;
        std::map<economy::RequestId, ClaimReceipt> claims;

        QuestId previous_id = 0;
        for (const auto &quest : snapshot.quests)
        {
            if (quest.definition.id <= previous_id || validate(quest.definition) != Error::none ||
                quest.progress.size() != quest.definition.objectives.size())
                return Error::invalid_snapshot;
            previous_id = quest.definition.id;
            for (std::size_t i = 0; i < quest.progress.size(); ++i)
                if (quest.progress[i] > quest.definition.objectives[i].target ||
                    !registry_.find(quest.definition.objectives[i].type)->validate(quest.definition.objectives[i], quest.progress[i]))
                    return Error::invalid_snapshot;
            const bool bound = detail::has_bindings(quest.definition);
            if ((!quest.claimed || !bound) && quest.fact_until != 0)
                return Error::invalid_snapshot;
            if (quest.claimed && bound && quest.fact_until != 0 && (snapshot.events.empty() ||
                quest.fact_until > snapshot.events.back().id))
                return Error::invalid_snapshot;
            quests.emplace(quest.definition.id, quest);     // 放入临时任务表
        }

        // 把任务进度清零，再按存档中的历史事件重新计算进度，验证保存的进度是否有依据
        auto reconstructed = quests;
        for (auto &entry : reconstructed)
            std::fill(entry.second.progress.begin(), entry.second.progress.end(), 0);
        EventId previous_event = 0;
        for (const auto &event : snapshot.events)
        {
            if (event.updates.size() > max_updates)
                return Error::invalid_snapshot;
            auto canonical = event;
            if (event.id <= previous_event || normalize_event(canonical) != Error::none || !(canonical == event))
                return Error::invalid_snapshot;
            previous_event = event.id;
            if (event.fact) {
                std::vector<ObjectiveUpdate> expected;
                if (detail::route_fact(*event.fact, quests, true, expected) != Error::none || expected != event.updates)
                    return Error::invalid_snapshot;
            }
            for (const auto &update : event.updates)
            {
                const auto found = reconstructed.find(update.quest);
                if (found == reconstructed.end() || (found->second.claimed && detail::has_bindings(found->second.definition) &&
                    event.id > found->second.fact_until) || advance(found->second, update, event.id, registry_) != Error::none)
                    return Error::invalid_snapshot;
            }
            events.emplace(event.id, event);
        }

        for (const auto &entry : quests)
            if (entry.second.fact_until != 0 && events.count(entry.second.fact_until) == 0)
                return Error::invalid_snapshot;
        for (const auto &entry : quests)
            if (entry.second.progress != reconstructed.at(entry.first).progress)
                return Error::invalid_snapshot;
        std::set<QuestId> claimed_quests;
        std::uint64_t previous_sequence = 0;
        for (const auto &receipt : snapshot.claims)
        {
            const auto &request = receipt.request;
            const auto found = quests.find(request.quest);
            const auto &transaction = receipt.settlement.transaction;
            if (request.request_id == 0 || request.destination == 0 || found == quests.end() || !found->second.claimed ||
                !complete(found->second) || !claimed_quests.insert(request.quest).second || receipt.settlement.sequence <= previous_sequence ||
                request.request_id != transaction.request_id || request.destination != transaction.destination ||
                !transaction.cost.currencies.empty() || transaction.reward.currencies != found->second.definition.reward.currencies ||
                transaction.reward.items != found->second.definition.reward.items || !claims.emplace(request.request_id, receipt).second)
                return Error::invalid_snapshot;
            previous_sequence = receipt.settlement.sequence;
        }
        for (const auto &entry : quests)
            if (entry.second.claimed != (claimed_quests.count(entry.first) != 0))
                return Error::invalid_snapshot;

        quests_.swap(quests);
        events_.swap(events);
        claims_.swap(claims);

        return Error::none;
    }
} // namespace designer::quests
