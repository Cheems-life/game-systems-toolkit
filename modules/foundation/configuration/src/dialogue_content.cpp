#include <designer/configuration/dialogue_content.hpp>
#include <designer/persistence/file_store.hpp>
#include "json_validation.hpp"
#include <stdexcept>
namespace designer::configuration
{
    namespace
    {
        using namespace detail;
        Value parse(std::string_view bytes, std::string_view source)
        {
            if (bytes.size() > dialogue_file_max_bytes)
                throw Failure{
                    {Error::limit, std::string(source), "$", {}, "dialogue resource exceeds 512 KiB"}};
            Sax sax(source);
            if (!nlohmann::json::sax_parse(bytes.begin(), bytes.end(), &sax))
                throw Failure{{Error::syntax, std::string(source), "$", {}, "JSON parser stopped"}};
            return std::move(sax.root);
        }
        std::string string(const Cursor &c, std::size_t limit)
        {
            if (!c.value.is_string())
                c.fail(Error::structure, "expected string");
            auto value = c.value.get<std::string>();
            if (value.empty() || value.size() > limit)
                c.fail(Error::range, "empty or oversized string");
            return value;
        }
        std::string key(const Cursor &c)
        {
            auto value = string(c, dialogue::max_key_bytes);
            if (!dialogue::valid_text_key(value))
                c.fail(Error::rule, "invalid text key");
            return value;
        }
        void schema(const Cursor &root)
        {
            if (positive<std::uint32_t>(root.at("schema_version")) != 1)
                root.at("schema_version")
                    .fail(Error::unsupported_version, "only schema_version 1 is supported");
        }
        Diagnostic file_error(const std::filesystem::path &path, persistence::FileError error)
        {
            return {error == persistence::FileError::too_large ? Error::limit : Error::io,
                    path.string(),
                    "$",
                    {},
                    persistence::file_error_name(error)};
        }
    } // namespace
    DialogueLoadResult decode_dialogue_content(std::string_view bytes, std::string_view source)
    {
        try
        {
            const auto parsed = parse(bytes, source);
            const Cursor root{parsed, "$", source};
            root.object({"schema_version", "content_version", "dialogue"});
            schema(root);
            const auto version = positive<std::uint64_t>(root.at("content_version"));
            auto d = root.at("dialogue");
            d.object({"id", "version", "entry", "completion_fact_type", "nodes"});
            dialogue::Definition definition{positive<std::uint32_t>(d.at("id")),
                                            positive<std::uint32_t>(d.at("version")),
                                            positive<std::uint32_t>(d.at("entry")),
                                            positive<std::uint32_t>(d.at("completion_fact_type")),
                                            {}};
            d.record_id = definition.id;
            const auto nodes = d.at("nodes");
            nodes.array(dialogue::max_nodes);
            for (std::size_t i = 0; i < nodes.value.size(); ++i)
            {
                auto n = nodes.record(i);
                // 先验证共有字段再读取 kind，避免缺字段变成 map::at 异常。
                if (!n.value.is_object() || !n.value.contains("kind"))
                    n.fail(Error::structure, "node requires kind");
                const auto kind = string(n.at("kind"), 16);
                if (kind == "end")
                {
                    n.object({"id", "kind"});
                    definition.nodes.push_back(
                        {positive<std::uint32_t>(n.at("id")), dialogue::NodeKind::end});
                    continue;
                }
                if (kind == "text")
                    n.object({"id", "kind", "speaker", "text_key", "next"});
                else if (kind == "choice")
                    n.object({"id", "kind", "speaker", "text_key", "options"});
                else
                    n.at("kind").fail(Error::rule, "expected text, choice or end");
                dialogue::Node node{positive<std::uint32_t>(n.at("id")),
                                    kind == "text" ? dialogue::NodeKind::text : dialogue::NodeKind::choice,
                                    positive<std::uint32_t>(n.at("speaker")), key(n.at("text_key"))};
                if (kind == "text")
                    node.next = positive<std::uint32_t>(n.at("next"));
                else
                {
                    const auto options = n.at("options");
                    options.array(dialogue::max_options);
                    for (std::size_t j = 0; j < options.value.size(); ++j)
                    {
                        const auto o = options.record(j);
                        o.object({"id", "text_key", "next"});
                        node.options.push_back({positive<std::uint32_t>(o.at("id")), key(o.at("text_key")),
                                                positive<std::uint32_t>(o.at("next"))});
                    }
                }
                definition.nodes.push_back(std::move(node));
            }
            try
            {
                definition = dialogue::validate_dialogue_definition(std::move(definition));
            }
            catch (const std::invalid_argument &e)
            {
                d.fail(Error::rule, e.what());
            }
            return {std::make_unique<const DialogueContent>(DialogueContent{version, std::move(definition)}),
                    {}};
        }
        catch (const Failure &failure)
        {
            return {nullptr, failure.diagnostic};
        }
    }
    TextLoadResult decode_dialogue_texts(std::string_view bytes, std::string_view source)
    {
        try
        {
            const auto parsed = parse(bytes, source);
            const Cursor root{parsed, "$", source};
            root.object({"schema_version", "content_version", "locale", "texts"});
            schema(root);
            TextTable table{
                positive<std::uint64_t>(root.at("content_version")), string(root.at("locale"), 32), {}};
            for (unsigned char c : table.locale)
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
                    root.at("locale").fail(Error::rule, "invalid locale identifier");
            const auto entries = root.at("texts");
            if (!entries.value.is_object())
                entries.fail(Error::structure, "expected text map");
            if (entries.value.size() > max_texts)
                entries.fail(Error::limit, "too many text entries");
            for (const auto &pair : entries.value.get<Value::Object>())
            {
                const Cursor entry{pair.second, entries.path + "." + pair.first, source};
                if (!dialogue::valid_text_key(pair.first))
                    entry.fail(Error::rule, "invalid text key");
                table.texts.emplace(pair.first, string(entry, max_text_bytes));
            }
            return {std::make_unique<const TextTable>(std::move(table)), {}};
        }
        catch (const Failure &failure)
        {
            return {nullptr, failure.diagnostic};
        }
    }
    DialogueLoadResult load_dialogue_content(const std::filesystem::path &path)
    {
        std::string bytes;
        const auto error = persistence::load_file(path, bytes, dialogue_file_max_bytes);
        if (error != persistence::FileError::none)
            return {nullptr, file_error(path, error)};
        return decode_dialogue_content(bytes, path.string());
    }
    TextLoadResult load_dialogue_texts(const std::filesystem::path &path)
    {
        std::string bytes;
        const auto error = persistence::load_file(path, bytes, dialogue_file_max_bytes);
        if (error != persistence::FileError::none)
            return {nullptr, file_error(path, error)};
        return decode_dialogue_texts(bytes, path.string());
    }
    std::optional<Diagnostic> validate_dialogue_text_references(const dialogue::Definition &definition,
                                                                const TextTable &table,
                                                                std::string_view source)
    {
        auto check = [&](const std::string &key, std::string path) -> std::optional<Diagnostic> {
            if (!table.find_text(key))
                return Diagnostic{Error::reference, std::string(source), std::move(path), definition.id,
                                  "missing text key: " + key};
            return {};
        };
        for (std::size_t i = 0; i < definition.nodes.size(); ++i)
        {
            const auto &node = definition.nodes[i];
            const auto path = "$.dialogue.nodes[" + std::to_string(i) + "]";
            if (node.kind != dialogue::NodeKind::end)
                if (auto error = check(node.text_key, path + ".text_key"))
                    return error;
            for (std::size_t j = 0; j < node.options.size(); ++j)
                if (auto error = check(node.options[j].text_key,
                                       path + ".options[" + std::to_string(j) + "].text_key"))
                    return error;
        }
        return {};
    }
} // namespace designer::configuration
