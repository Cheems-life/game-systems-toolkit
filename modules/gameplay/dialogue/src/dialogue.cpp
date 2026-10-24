#include <designer/dialogue/dialogue.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
namespace designer::dialogue
{
    bool valid_text_key(const std::string &key) noexcept
    {
        if (key.empty() || key.size() > max_key_bytes)
            return false;
        for (unsigned char c : key)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                  c == '-' || c == '.'))
                return false;
        return true;
    }
    const Node *find_dialogue_node(const Definition &d, NodeId id) noexcept
    {
        const auto found = std::lower_bound(d.nodes.begin(), d.nodes.end(), id,
                                            [](const Node &node, NodeId value) { return node.id < value; });
        return found == d.nodes.end() || found->id != id ? nullptr : &*found;
    }
    Definition validate_dialogue_definition(Definition d)
    {
        if (!d.id || !d.version || !d.entry || !d.completion_fact_type || d.nodes.empty() ||
            d.nodes.size() > max_nodes)
            throw std::invalid_argument("Invalid dialogue identity or node count");
        std::sort(d.nodes.begin(), d.nodes.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        NodeId previous = 0;
        std::map<NodeId, std::vector<NodeId>> reverse;
        for (const auto &node : d.nodes)
        {
            if (node.id <= previous)
                throw std::invalid_argument("Zero or duplicate dialogue node ID");
            previous = node.id;
            if (node.kind == NodeKind::end)
            {
                if (node.speaker || !node.text_key.empty() || node.next || !node.options.empty())
                    throw std::invalid_argument("End node has presentation or outgoing fields");
            }
            else if (node.kind == NodeKind::text || node.kind == NodeKind::choice)
            {
                if (!node.speaker || !valid_text_key(node.text_key))
                    throw std::invalid_argument("Invalid speaker or text key at node " +
                                                std::to_string(node.id));
                if (node.kind == NodeKind::text && (!node.next || !node.options.empty()))
                    throw std::invalid_argument("Text node needs one next and no options");
                if (node.kind == NodeKind::choice &&
                    (node.next || node.options.empty() || node.options.size() > max_options))
                    throw std::invalid_argument("Choice node needs options and no direct next");
                std::set<OptionId> options;
                for (const auto &option : node.options)
                    if (!option.id || !option.next || !valid_text_key(option.text_key) ||
                        !options.insert(option.id).second)
                        throw std::invalid_argument("Invalid or duplicate option");
            }
            else
                throw std::invalid_argument("Unknown dialogue node kind");
            auto edge = [&](NodeId to) {
                if (!find_dialogue_node(d, to))
                    throw std::invalid_argument("Unknown dialogue target " + std::to_string(to));
                reverse[to].push_back(node.id);
            };
            if (node.next)
                edge(node.next);
            for (const auto &option : node.options)
                edge(option.next);
        }
        if (!find_dialogue_node(d, d.entry))
            throw std::invalid_argument("Unknown dialogue entry");
        // 正向检查入口可达，反向检查每个节点都存在结束路径；允许玩家手动选择的有出口循环。
        std::set<NodeId> reached;
        std::vector<NodeId> pending{d.entry};
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            const auto id = pending[index];
            if (!reached.insert(id).second)
                continue;
            const auto &node = *find_dialogue_node(d, id);
            if (node.next)
                pending.push_back(node.next);
            for (const auto &option : node.options)
                pending.push_back(option.next);
        }
        if (reached.size() != d.nodes.size())
            throw std::invalid_argument("Unreachable dialogue node");
        reached.clear();
        pending.clear();
        for (const auto &node : d.nodes)
            if (node.kind == NodeKind::end)
                pending.push_back(node.id);
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            const auto id = pending[index];
            if (!reached.insert(id).second)
                continue;
            const auto found = reverse.find(id);
            if (found != reverse.end())
                for (auto from : found->second)
                    pending.push_back(from);
        }
        if (reached.size() != d.nodes.size())
            throw std::invalid_argument("Dialogue branch cannot reach any end");
        return d;
    }
    Catalog::Catalog(std::vector<Definition> definitions)
    {
        if (definitions.size() > max_dialogues)
            throw std::invalid_argument("Too many loaded dialogues");
        for (auto &d : definitions)
        {
            auto checked = std::make_shared<const Definition>(validate_dialogue_definition(std::move(d)));
            if (!definitions_.emplace(checked->id, checked).second)
                throw std::invalid_argument("Duplicate dialogue ID");
        }
    }
    const Definition *Catalog::find(DialogueId id) const noexcept
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() ? nullptr : found->second.get();
    }
    void Catalog::replace_dialogue_definition(Definition definition)
    {
        auto checked =
            std::make_shared<const Definition>(validate_dialogue_definition(std::move(definition)));
        const auto found = definitions_.find(checked->id);
        if (found != definitions_.end())
        {
            if (*found->second == *checked)
                return;
            if (checked->version <= found->second->version)
                throw std::invalid_argument("Changed dialogue requires a higher version");
            found->second = std::move(checked);
        }
        else
        {
            if (definitions_.size() >= max_dialogues)
                throw std::invalid_argument("Too many loaded dialogues");
            const auto id = checked->id;
            definitions_.emplace(id, std::move(checked));
        }
    }
    bool Catalog::remove_dialogue_definition(DialogueId id) noexcept
    {
        return definitions_.erase(id) != 0;
    }
    bool valid_dialogue_command(const Command &c) noexcept
    {
        if (!c.request_id)
            return false;
        if (c.action == Action::start)
            return c.dialogue && !c.session && !c.expected_node && !c.expected_revision && !c.option;
        if (c.dialogue || !c.session || !c.expected_node || !c.expected_revision)
            return false;
        if (c.action == Action::choose)
            return c.option != 0;
        return !c.option &&
               (c.action == Action::advance || c.action == Action::finish || c.action == Action::cancel);
    }
    const char *error_name(Error error) noexcept
    {
        switch (error)
        {
#define DIALOGUE_ERROR(e)                                                                                    \
    case Error::e:                                                                                           \
        return #e
            DIALOGUE_ERROR(none);
            DIALOGUE_ERROR(invalid_command);
            DIALOGUE_ERROR(request_conflict);
            DIALOGUE_ERROR(unknown_dialogue);
            DIALOGUE_ERROR(unknown_session);
            DIALOGUE_ERROR(active_session);
            DIALOGUE_ERROR(session_closed);
            DIALOGUE_ERROR(stale_view);
            DIALOGUE_ERROR(wrong_node_kind);
            DIALOGUE_ERROR(unknown_option);
            DIALOGUE_ERROR(history_full);
            DIALOGUE_ERROR(session_limit);
            DIALOGUE_ERROR(invalid_event);
            DIALOGUE_ERROR(event_id_exhausted);
            DIALOGUE_ERROR(fact_rejected);
            DIALOGUE_ERROR(invalid_snapshot);
#undef DIALOGUE_ERROR
        }
        return "unrecognized_error";
    }
    Book::Book(std::size_t limit) : history_limit_(limit)
    {
        if (!limit || limit > max_history)
            throw std::invalid_argument("Invalid dialogue history limit");
    }
    std::optional<SessionView> Book::query_dialogue_session(SessionId id) const noexcept
    {
        const auto found = sessions_.find(id);
        if (found == sessions_.end())
            return std::nullopt;
        const auto &s = found->second;
        return SessionView{s.id, *s.definition, *find_dialogue_node(*s.definition, s.node), s.status,
                           s.revision};
    }
    const Receipt *Book::find_dialogue_receipt(RequestId id) const noexcept
    {
        const auto found = commands_.find(id);
        return found == commands_.end() ? nullptr : &found->second;
    }
    Result Book::perform_command(const Catalog &catalog, const Command &c, EventId event)
    {
        if (!valid_dialogue_command(c))
            return {Error::invalid_command};
        if (commands_.size() >= history_limit_)
            return {Error::history_full};
        SessionId id = c.session;
        if (c.action == Action::start)
        {
            if (event)
                return {Error::invalid_event};
            for (const auto &pair : sessions_)
                if (pair.second.status == Status::active)
                    return {Error::active_session};
            if (sessions_.size() >= max_sessions)
                return {Error::session_limit};
            const auto found = catalog.definitions_.find(c.dialogue);
            if (found == catalog.definitions_.end())
                return {Error::unknown_dialogue};
            id = c.request_id;
            sessions_.emplace(id, Session{id, found->second, found->second->entry, Status::active, 1});
        }
        else
        {
            const auto found = sessions_.find(id);
            if (found == sessions_.end())
                return {Error::unknown_session};
            auto &s = found->second;
            if (s.status != Status::active)
                return {Error::session_closed};
            if (c.expected_node != s.node || c.expected_revision != s.revision)
                return {Error::stale_view};
            if (s.revision == std::numeric_limits<std::uint64_t>::max())
                return {Error::invalid_command};
            const auto &node = *find_dialogue_node(*s.definition, s.node);
            if (c.action == Action::finish)
            {
                if (node.kind != NodeKind::end)
                    return {Error::wrong_node_kind};
                EventId previous = 0;
                for (const auto &pair : commands_)
                    previous = std::max(previous, pair.second.completion_event);
                if (!event || event <= previous)
                    return {Error::invalid_event};
                s.status = Status::completed;
            }
            else
            {
                if (event)
                    return {Error::invalid_event};
                if (c.action == Action::cancel)
                    s.status = Status::cancelled;
                else if (c.action == Action::advance)
                {
                    if (node.kind != NodeKind::text)
                        return {Error::wrong_node_kind};
                    s.node = node.next;
                }
                else
                {
                    if (node.kind != NodeKind::choice)
                        return {Error::wrong_node_kind};
                    const auto option = std::find_if(node.options.begin(), node.options.end(),
                                                     [&](const auto &o) { return o.id == c.option; });
                    if (option == node.options.end())
                        return {Error::unknown_option};
                    s.node = option->next;
                }
            }
            ++s.revision;
        }
        const auto &s = sessions_.at(id);
        Receipt receipt{c, commands_.size() + 1, id, s.node, s.status, s.revision, event};
        commands_.emplace(c.request_id, receipt);
        return {Error::none, receipt, false};
    }
    Result Book::execute_dialogue_command(const Catalog &catalog, const Command &c, EventId event)
    {
        if (!valid_dialogue_command(c))
            return {Error::invalid_command};
        if (const auto *prior = find_dialogue_receipt(c.request_id))
        {
            if (!(prior->command == c))
                return {Error::request_conflict};
            return {Error::none, *prior, true};
        }
        // 所有分配和转换发生于候选；只有结果准备完毕后才交换。
        auto candidate = *this;
        auto result = candidate.perform_command(catalog, c, event);
        if (result.error == Error::none)
        {
            sessions_.swap(candidate.sessions_);
            commands_.swap(candidate.commands_);
        }
        static_assert(std::is_nothrow_move_constructible<Result>::value,
                      "Return must not allocate after commit");
        return result;
    }
    Snapshot Book::snapshot() const
    {
        Snapshot snapshot;
        for (const auto &pair : sessions_)
        {
            const auto &s = pair.second;
            snapshot.sessions.push_back({s.id, *s.definition, s.node, s.status, s.revision});
        }
        for (const auto &pair : commands_)
            snapshot.commands.push_back(pair.second);
        std::sort(snapshot.commands.begin(), snapshot.commands.end(),
                  [](const auto &a, const auto &b) { return a.sequence < b.sequence; });
        return snapshot;
    }
    Error Book::restore(const Snapshot &snapshot)
    {
        if (snapshot.sessions.size() > max_sessions || snapshot.commands.size() > history_limit_)
            return Error::invalid_snapshot;
        std::map<SessionId, const SessionSnapshot *> definitions;
        SessionId previous = 0;
        for (const auto &s : snapshot.sessions)
        {
            if (s.id <= previous)
                return Error::invalid_snapshot;
            previous = s.id;
            try
            {
                if (!(validate_dialogue_definition(s.definition) == s.definition))
                    return Error::invalid_snapshot;
            }
            catch (const std::invalid_argument &)
            {
                return Error::invalid_snapshot;
            }
            definitions.emplace(s.id, &s);
        }
        // 回放成功命令并逐条核对回执，拒绝“最终快照看起来合法、历史却不一致”的恢复。
        Book candidate(history_limit_);
        for (const auto &r : snapshot.commands)
        {
            if (candidate.commands_.count(r.command.request_id))
                return Error::invalid_snapshot;
            Catalog start({});
            if (r.command.action == Action::start)
            {
                const auto found = definitions.find(r.command.request_id);
                if (found == definitions.end() || found->second->definition.id != r.command.dialogue)
                    return Error::invalid_snapshot;
                start.replace_dialogue_definition(found->second->definition);
            }
            const auto result = candidate.perform_command(start, r.command, r.completion_event);
            if (result.error != Error::none || !(*result.receipt == r))
                return Error::invalid_snapshot;
        }
        if (!(candidate.snapshot() == snapshot))
            return Error::invalid_snapshot;
        sessions_.swap(candidate.sessions_);
        commands_.swap(candidate.commands_);
        return Error::none;
    }
} // namespace designer::dialogue
