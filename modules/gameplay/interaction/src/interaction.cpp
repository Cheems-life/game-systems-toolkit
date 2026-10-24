#include <designer/interaction/interaction.hpp>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
namespace designer::interaction
{
    namespace
    {
        bool valid_mode(Mode mode) noexcept { return mode == Mode::once || mode == Mode::repeatable; }
    }

    Catalog::Catalog(std::vector<Definition> definitions)
    {
        if (definitions.size() > max_points)
            throw std::invalid_argument("Too many interaction points");
        for (const auto &d : definitions)
            if (d.id == 0 || d.version == 0 || d.fact_type == 0 || !valid_mode(d.mode) || !definitions_.emplace(d.id, d).second)
                throw std::invalid_argument("Invalid or duplicate interaction definition");
    }

    const Definition *Catalog::find(PointId id) const noexcept
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() ? nullptr : &found->second;
    }

    const char *error_name(Error error) noexcept
    {
        switch (error)
        {
#define INTERACTION_ERROR(e) \
    case Error::e:           \
        return #e
            INTERACTION_ERROR(none);
            INTERACTION_ERROR(invalid_request);
            INTERACTION_ERROR(unknown_point);
            INTERACTION_ERROR(disabled);
            INTERACTION_ERROR(already_completed);
            INTERACTION_ERROR(configuration_conflict);
            INTERACTION_ERROR(request_conflict);
            INTERACTION_ERROR(history_full);
            INTERACTION_ERROR(invalid_event);
            INTERACTION_ERROR(event_id_exhausted);
            INTERACTION_ERROR(fact_rejected);
            INTERACTION_ERROR(invalid_snapshot);
#undef INTERACTION_ERROR
        }
        return "unrecognized_error";
    }

    Book::Book(std::size_t limit) : history_limit_(limit)
    {
        if (limit == 0 || limit > max_history)
            throw std::invalid_argument("Invalid interaction history limit");
    }

    View Book::check_interaction_availability(const Catalog &catalog, PointId point) const noexcept              // 当前能否进行一次新交互？
    {
        if (point == 0)
            return {false, Error::invalid_request};
        const auto previous = modes_.find(point);

        if (previous != modes_.end() && previous->second == Mode::once)
            return {false, Error::already_completed};
        const auto *definition = catalog.find(point);
        if (!definition)
            return {false, Error::unknown_point};
        if (!definition->enabled)
            return {false, Error::disabled};
        if (previous != modes_.end() && previous->second != definition->mode)
            return {false, Error::configuration_conflict};
        if (records_.size() >= history_limit_)
            return {false, Error::history_full};
        return {true, Error::none};
    }

    const Receipt *Book::find_interaction_receipt(RequestId id) const noexcept
    {
        const auto found = records_.find(id);
        return found == records_.end() ? nullptr : &found->second;
    }

    Result Book::execute_interaction(const Catalog &catalog, const Request &request, EventId event)             // 交互规则、本次操作哪个交互点、成功操作关联的可信事件序号
    {
        if (request.request_id == 0 || request.point == 0)
            return {Error::invalid_request};

        if (const auto *prior = find_interaction_receipt(request.request_id))                // 这个请求以前成功过吗？
        {
            if (!(prior->request == request))
                return {Error::request_conflict};
            return {Error::none, *prior, true};                             // 已存在，返回原凭证
        }

        const auto view = check_interaction_availability(catalog, request.point);                    // 这个交互点现在允许新操作吗
        if (!view.allowed)
            return {view.reason};

        if (event == 0 || event <= last_event_)                             // 检查事件序号是否向前推进
            return {Error::invalid_event};

        const auto &d = *catalog.find(request.point);
        Receipt record{request, d.version, d.mode, d.fact_type, event};     // 生成一张成功凭证

        auto records = records_;
        auto modes = modes_;

        records.emplace(request.request_id, record);
        modes.emplace(request.point, d.mode);

        Result output{Error::none, record, false};
        static_assert(std::is_nothrow_move_constructible<Result>::value, "Result return must not allocate");
        records_.swap(records);
        modes_.swap(modes);
        last_event_ = event;
        return output;
    }

    Snapshot Book::snapshot() const
    {
        Snapshot result;
        for (const auto &pair : records_)
            result.push_back(pair.second);
        std::sort(result.begin(), result.end(), [](const auto &a, const auto &b)
                  { return a.event_id < b.event_id; });
        return result;
    }

    Error Book::restore(const Snapshot &snapshot)
    {
        if (snapshot.size() > history_limit_)
            return Error::invalid_snapshot;
        std::map<RequestId, Receipt> records;
        std::map<PointId, Mode> modes;
        EventId previous = 0;
        for (const auto &r : snapshot)
        {
            if (r.request.request_id == 0 || r.request.point == 0 || r.version == 0 || r.fact_type == 0 ||
                !valid_mode(r.mode) || r.event_id <= previous || !records.emplace(r.request.request_id, r).second)
                return Error::invalid_snapshot;
            const auto inserted = modes.emplace(r.request.point, r.mode);
            if (!inserted.second && (inserted.first->second != r.mode || r.mode == Mode::once))
                return Error::invalid_snapshot;
            previous = r.event_id;
        }
        records_.swap(records);
        modes_.swap(modes);
        last_event_ = previous;
        return Error::none;
    }
} // namespace designer::interaction
