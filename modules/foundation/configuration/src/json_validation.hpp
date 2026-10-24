#pragma once
#include "json_reader.hpp"
#include <algorithm>
#include <limits>
#include <set>
namespace designer::configuration::detail
{
    struct Cursor
    {
        const Value &value;
        std::string path;
        std::string_view source;
        std::optional<std::uint32_t> record_id = std::nullopt;
        [[noreturn]] void fail(Error code, std::string message) const
        {
            throw Failure{{code, std::string(source), path, record_id, std::move(message)}};
        }
        Cursor at(const char *key) const
        {
            return {value.at(key), path + "." + key, source, record_id};
        }
        Cursor at(std::size_t i) const
        {
            return {value.at(i), path + "[" + std::to_string(i) + "]", source, record_id};
        }
        Cursor record(std::size_t i) const
        {
            auto row = at(i);
            if (row.value.is_object())
            {
                const auto &object = row.value.get<Value::Object>();
                const auto found = object.find("id");
                if (found != object.end() && found->second.is_number_unsigned())
                {
                    const auto id = found->second.get<std::uint64_t>();
                    if (id > 0 && id <= std::numeric_limits<std::uint32_t>::max())
                        row.record_id = static_cast<std::uint32_t>(id);
                }
            }
            return row;
        }
        // 所有列均显式必填；拒绝未知列，避免拼写错误被当成默认值。
        void object(std::initializer_list<const char *> fields) const
        {
            if (!value.is_object())
                fail(Error::structure, "expected object");
            for (const auto &entry : value.get<Value::Object>())
            {
                if (std::none_of(fields.begin(), fields.end(),
                                 [&](const char *f) { return entry.first == f; }))
                    Cursor{entry.second, path + "." + entry.first, source, record_id}.fail(Error::structure,
                                                                                           "unknown field");
            }
            for (const auto *key : fields)
                if (!value.contains(key))
                    throw Failure{{Error::structure, std::string(source), path + "." + key, record_id,
                                   "missing field"}};
        }
        void array(std::size_t limit) const
        {
            if (!value.is_array())
                fail(Error::structure, "expected array");
            if (value.size() > limit)
                fail(Error::limit, "too many entries; maximum " + std::to_string(limit));
        }
    };

    template <class T> T positive(const Cursor &c)
    {
        // 不通过浮点/字符串中转，保留完整 uint64 精度，拒绝负数及 1.0/1e0。
        if (!c.value.is_number_unsigned())
            c.fail(Error::range, "expected positive integer");
        const auto n = c.value.get<std::uint64_t>();
        if (n == 0 || n > std::numeric_limits<T>::max())
            c.fail(Error::range, "positive integer out of range");
        return static_cast<T>(n);
    }
    inline void unique(std::set<std::uint32_t> &ids, std::uint32_t id, const Cursor &c)
    {
        if (!ids.insert(id).second)
            c.fail(Error::duplicate, "duplicate ID " + std::to_string(id));
    }

} // namespace designer::configuration::detail
