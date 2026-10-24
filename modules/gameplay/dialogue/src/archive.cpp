#include <designer/dialogue/archive.hpp>
#include <type_traits>
namespace designer::dialogue
{
    static_assert(std::is_nothrow_move_assignable<Snapshot>::value, "Archive commit must not allocate");
    namespace
    {
        constexpr std::string_view magic = "DDLGARCH";
        std::uint64_t checksum(std::string_view bytes) noexcept
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (unsigned char c : bytes)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
            return hash;
        }
        struct Writer
        {
            std::string bytes;
            void number(std::uint64_t value, unsigned width)
            {
                for (unsigned i = 0; i < width; ++i)
                    bytes.push_back(static_cast<char>((value >> (8 * i)) & 255));
            }
            void key(const std::string &value)
            {
                number(value.size(), 4);
                bytes += value;
            }
            void definition(const Definition &d)
            {
                number(d.id, 4);
                number(d.version, 4);
                number(d.entry, 4);
                number(d.completion_fact_type, 4);
                number(d.nodes.size(), 4);
                for (const auto &n : d.nodes)
                {
                    number(n.id, 4);
                    number(static_cast<unsigned>(n.kind), 4);
                    number(n.speaker, 4);
                    key(n.text_key);
                    number(n.next, 4);
                    number(n.options.size(), 4);
                    for (const auto &o : n.options)
                    {
                        number(o.id, 4);
                        key(o.text_key);
                        number(o.next, 4);
                    }
                }
            }
        };
        struct Reader
        {
            std::string_view bytes;
            std::size_t pos = 0;
            bool ok = true;
            std::uint64_t number(unsigned width)
            {
                if (pos > bytes.size() || width > bytes.size() - pos)
                {
                    ok = false;
                    return 0;
                }
                std::uint64_t value = 0;
                for (unsigned i = 0; i < width; ++i)
                    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[pos++])) << (8 * i);
                return value;
            }
            std::uint32_t u32()
            {
                return static_cast<std::uint32_t>(number(4));
            }
            std::string key()
            {
                const auto length = u32();
                if (!ok || length > max_key_bytes || length > bytes.size() - pos)
                {
                    ok = false;
                    return {};
                }
                std::string value(bytes.substr(pos, length));
                pos += length;
                return value;
            }
            Definition definition()
            {
                Definition d{u32(), u32(), u32(), u32(), {}};
                const auto count = u32();
                if (!ok || count > max_nodes)
                {
                    ok = false;
                    return d;
                }
                for (std::uint32_t i = 0; i < count && ok; ++i)
                {
                    Node n{u32(), static_cast<NodeKind>(u32()), u32(), key(), u32(), {}};
                    const auto options = u32();
                    if (!ok || options > max_options)
                    {
                        ok = false;
                        break;
                    }
                    for (std::uint32_t j = 0; j < options && ok; ++j)
                        n.options.push_back({u32(), key(), u32()});
                    d.nodes.push_back(std::move(n));
                }
                return d;
            }
        };
        bool valid(const Snapshot &snapshot)
        {
            Book book(max_history);
            return book.restore(snapshot) == Error::none;
        }
    } // namespace
    const char *archive_error_name(ArchiveError error) noexcept
    {
        switch (error)
        {
        case ArchiveError::none:
            return "none";
        case ArchiveError::too_large:
            return "too_large";
        case ArchiveError::malformed:
            return "malformed";
        case ArchiveError::unsupported_version:
            return "unsupported_version";
        case ArchiveError::checksum_mismatch:
            return "checksum_mismatch";
        }
        return "unrecognized_error";
    }
    ArchiveError encode_archive(const Snapshot &snapshot, std::string &output)
    {
        if (!valid(snapshot))
            return ArchiveError::malformed;
        Writer w;
        w.bytes = magic;
        w.number(1, 4);
        w.number(snapshot.sessions.size(), 4);
        for (const auto &s : snapshot.sessions)
        {
            w.number(s.id, 8);
            w.definition(s.definition);
            w.number(s.node, 4);
            w.number(static_cast<unsigned>(s.status), 4);
            w.number(s.revision, 8);
        }
        w.number(snapshot.commands.size(), 4);
        for (const auto &r : snapshot.commands)
        {
            const auto &c = r.command;
            w.number(c.request_id, 8);
            w.number(static_cast<unsigned>(c.action), 4);
            w.number(c.dialogue, 4);
            w.number(c.session, 8);
            w.number(c.expected_node, 4);
            w.number(c.expected_revision, 8);
            w.number(c.option, 4);
            w.number(r.sequence, 8);
            w.number(r.session, 8);
            w.number(r.node, 4);
            w.number(static_cast<unsigned>(r.status), 4);
            w.number(r.revision, 8);
            w.number(r.completion_event, 8);
        }
        w.number(checksum(w.bytes), 8);
        if (w.bytes.size() > archive_max_bytes)
            return ArchiveError::too_large;
        output.swap(w.bytes);
        return ArchiveError::none;
    }
    ArchiveError decode_archive(std::string_view bytes, Snapshot &output)
    {
        if (bytes.size() > archive_max_bytes)
            return ArchiveError::too_large;
        if (bytes.size() < 28 || bytes.substr(0, 8) != magic)
            return ArchiveError::malformed;
        Reader footer{bytes.substr(bytes.size() - 8)};
        if (footer.number(8) != checksum(bytes.substr(0, bytes.size() - 8)))
            return ArchiveError::checksum_mismatch;
        Reader r{bytes.substr(0, bytes.size() - 8), 8};
        if (r.u32() != 1)
            return ArchiveError::unsupported_version;
        Snapshot result;
        const auto sessions = r.u32();
        if (!r.ok || sessions > max_sessions)
            return ArchiveError::malformed;
        for (std::uint32_t i = 0; i < sessions && r.ok; ++i)
        {
            SessionSnapshot s{r.number(8), r.definition(), r.u32(), static_cast<Status>(r.u32()),
                              r.number(8)};
            result.sessions.push_back(std::move(s));
        }
        const auto commands = r.u32();
        if (!r.ok || commands > max_history)
            return ArchiveError::malformed;
        for (std::uint32_t i = 0; i < commands && r.ok; ++i)
        {
            Command c{r.number(8), static_cast<Action>(r.u32()), r.u32(), r.number(8), r.u32(), r.number(8),
                      r.u32()};
            result.commands.push_back({c, r.number(8), r.number(8), r.u32(), static_cast<Status>(r.u32()),
                                       r.number(8), r.number(8)});
        }
        if (!r.ok || r.pos != r.bytes.size() || !valid(result))
            return ArchiveError::malformed;
        output = std::move(result);
        return ArchiveError::none;
    }
} // namespace designer::dialogue
