#include <designer/configuration/bundle.hpp>
#include <designer/persistence/file_store.hpp>
#include "json_reader.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace designer::configuration
{
    namespace
    {
        using detail::Failure;
        using detail::Sax;
        using detail::Value;
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
            Cursor at(const char *key) const { return {value.at(key), path + "." + key, source, record_id}; }
            Cursor at(std::size_t i) const { return {value.at(i), path + "[" + std::to_string(i) + "]", source, record_id}; }
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
                    if (std::none_of(fields.begin(), fields.end(), [&](const char *f)
                                     { return entry.first == f; }))
                        Cursor{entry.second, path + "." + entry.first, source, record_id}.fail(Error::structure, "unknown field");
                }
                for (const auto *key : fields)
                    if (!value.contains(key))
                        throw Failure{{Error::structure, std::string(source), path + "." + key, record_id, "missing field"}};
            }
            void array(std::size_t limit) const
            {
                if (!value.is_array())
                    fail(Error::structure, "expected array");
                if (value.size() > limit)
                    fail(Error::limit, "too many entries; maximum " + std::to_string(limit));
            }
        };

        template <class T>
        T positive(const Cursor &c)
        {
            // 不通过浮点/字符串中转，保留完整 uint64 精度，拒绝负数及 1.0/1e0。
            if (!c.value.is_number_unsigned())
                c.fail(Error::range, "expected positive integer");
            const auto n = c.value.get<std::uint64_t>();
            if (n == 0 || n > std::numeric_limits<T>::max())
                c.fail(Error::range, "positive integer out of range");
            return static_cast<T>(n);
        }
        void unique(std::set<std::uint32_t> &ids, std::uint32_t id, const Cursor &c)
        {
            if (!ids.insert(id).second)
                c.fail(Error::duplicate, "duplicate ID " + std::to_string(id));
        }
        std::vector<economy::CurrencyAmount> amounts(const Cursor &c)
        {
            c.array(economy::max_entries_per_list);
            std::vector<economy::CurrencyAmount> result;
            std::set<std::uint32_t> ids;
            for (std::size_t i = 0; i < c.value.size(); ++i)
            {
                auto row = c.at(i);
                row.object({"currency", "amount"});
                const auto id = positive<std::uint32_t>(row.at("currency"));
                unique(ids, id, row.at("currency"));
                result.push_back({id, positive<std::uint64_t>(row.at("amount"))});
            }
            return result;
        }
        economy::RewardBundle reward(const Cursor &c)
        {
            c.object({"currencies", "items"});
            economy::RewardBundle result{amounts(c.at("currencies")), {}};
            const auto items = c.at("items");
            items.array(economy::max_entries_per_list);
            std::set<std::uint32_t> ids;
            for (std::size_t i = 0; i < items.value.size(); ++i)
            {
                auto row = items.at(i);
                row.object({"item", "quantity"});
                const auto id = positive<std::uint32_t>(row.at("item"));
                unique(ids, id, row.at("item"));
                result.items.push_back({id, positive<std::uint32_t>(row.at("quantity"))});
            }
            return result;
        }
        void currency_references(const Cursor &c, const economy::CurrencyCatalog &catalog)
        {
            for (std::size_t i = 0; i < c.value.size(); ++i)
            {
                const auto key = c.at(i).at("currency");
                if (!catalog.find(positive<std::uint32_t>(key)))
                    key.fail(Error::reference, "unknown currency ID");
            }
        }
        void reward_references(const Cursor &c, const inventory::ItemCatalog &items, const economy::CurrencyCatalog &currencies)
        {
            currency_references(c.at("currencies"), currencies);
            const auto list = c.at("items");
            for (std::size_t i = 0; i < list.value.size(); ++i)
            {
                const auto key = list.at(i).at("item");
                if (!items.find(positive<std::uint32_t>(key)))
                    key.fail(Error::reference, "unknown item ID");
            }
        }
        // 局部领域构造仍执行已有规则，不复制“价格/奖励不能为空”等领域契约。
        template <class F>
        void domain(const Cursor &row, F &&construct)
        {
            try
            {
                construct();
            }
            catch (const std::invalid_argument &e)
            {
                row.fail(Error::rule, e.what());
            }
        }
    } // namespace

    const char *error_name(Error e) noexcept
    {
        switch (e)
        {
        case Error::io:
            return "io";
        case Error::syntax:
            return "syntax";
        case Error::structure:
            return "structure";
        case Error::range:
            return "range";
        case Error::duplicate:
            return "duplicate";
        case Error::unsupported_version:
            return "unsupported_version";
        case Error::reference:
            return "reference";
        case Error::rule:
            return "rule";
        case Error::limit:
            return "limit";
        }
        return "unrecognized_error";
    }
    Bundle::Bundle(std::uint64_t version, inventory::ItemCatalog items, economy::CurrencyCatalog currencies,
                   commerce::ProductCatalog products, quests::QuestCatalog quests, quests::ObjectiveRegistry registry, interaction::Catalog interactions)
        : version_(version), items_(std::move(items)), currencies_(std::move(currencies)),
          products_(std::move(products)), quests_(std::move(quests)), registry_(std::move(registry)), interactions_(std::move(interactions)) {}

    LoadResult decode(std::string_view bytes, const quests::ObjectiveRegistry &registry, std::string_view source)
    {
        try
        {
            if (bytes.size() > max_bytes)
                throw Failure{{Error::limit, std::string(source), "$", {}, "configuration exceeds 4 MiB"}};
            Sax sax(source);
            // 只借用成熟库的语法/UTF-8/数字解析，不创建其 DOM。
            if (!nlohmann::json::sax_parse(bytes.begin(), bytes.end(), &sax))
                throw Failure{{Error::syntax, std::string(source), "$", {}, "JSON parser stopped"}};
            const Cursor root{sax.root, "$", source};
            if (!root.value.is_object()) root.fail(Error::structure, "expected object");
        if (!root.value.contains("schema_version")) {
            throw Failure{{Error::structure, std::string(source), "$.schema_version", {}, "missing field"}};
        }
            const auto schema = positive<std::uint32_t>(root.at("schema_version"));
            if (schema != 1 && schema != 2 && schema != 3)
                root.at("schema_version").fail(Error::unsupported_version, "only schema_version 1, 2 and 3 are supported");
        if (schema == 3) root.object({"schema_version", "content_version", "items", "currencies", "products", "quests", "interactions"});
        else root.object({"schema_version", "content_version", "items", "currencies", "products", "quests"});
            const auto version = positive<std::uint64_t>(root.at("content_version"));
            std::vector<inventory::ItemDefinition> item_defs;
            auto table = root.at("items");
            table.array(max_items);
            std::set<std::uint32_t> ids;
            for (std::size_t i = 0; i < table.value.size(); ++i)
            {
                auto row = table.record(i);
                row.object({"id", "max_stack"});
                const auto id = positive<std::uint32_t>(row.at("id"));
                row.record_id = id;
                unique(ids, id, row.at("id"));
                item_defs.push_back({id, positive<std::uint32_t>(row.at("max_stack"))});
            }
            inventory::ItemCatalog items(std::move(item_defs));
            std::vector<economy::CurrencyDefinition> currency_defs;
            const auto currency_table = root.at("currencies");
            currency_table.array(economy::max_currencies);
            ids.clear();
            for (std::size_t i = 0; i < currency_table.value.size(); ++i)
            {
                auto row = currency_table.record(i);
                row.object({"id", "max_balance"});
                const auto id = positive<std::uint32_t>(row.at("id"));
                row.record_id = id;
                unique(ids, id, row.at("id"));
                currency_defs.push_back({id, positive<std::uint64_t>(row.at("max_balance"))});
            }
            economy::CurrencyCatalog currencies(std::move(currency_defs));
            std::vector<commerce::ProductDefinition> product_defs;
            const auto products = root.at("products");
            products.array(commerce::max_products);
            ids.clear();
            for (std::size_t i = 0; i < products.value.size(); ++i)
            {
                auto row = products.record(i);
                row.object({"id", "version", "available", "unit_cost", "unit_reward"});
                const auto id = positive<std::uint32_t>(row.at("id"));
                row.record_id = id;
                unique(ids, id, row.at("id"));
                const auto cost = row.at("unit_cost");
                cost.object({"currencies"});
                const auto available = row.at("available");
                if (!available.value.is_boolean())
                    available.fail(Error::structure, "expected boolean");
                commerce::ProductDefinition d{id, positive<std::uint32_t>(row.at("version")), available.value.get<bool>(), {amounts(cost.at("currencies"))}, reward(row.at("unit_reward"))};
                domain(row, [&]
                       { const commerce::ProductCatalog check({d}); });
                currency_references(cost.at("currencies"), currencies);
                reward_references(row.at("unit_reward"), items, currencies);
                product_defs.push_back(std::move(d));
            }
            std::vector<quests::QuestDefinition> quest_defs;
            const auto tasks = root.at("quests");
            tasks.array(quests::max_quests);
            ids.clear();
            for (std::size_t i = 0; i < tasks.value.size(); ++i)
            {
                auto row = tasks.record(i);
                row.object({"id", "version", "minimum_level", "objectives", "reward", "policy", "title"});
                const auto id = positive<std::uint32_t>(row.at("id"));
                row.record_id = id;
                unique(ids, id, row.at("id"));
                const auto policy = row.at("policy");
                if (!policy.value.is_string() || policy.value.get<std::string>() != "all")
                    policy.fail(Error::rule, "only policy all is supported");
                const auto title = row.at("title");
                if (!title.value.is_string())
                    title.fail(Error::structure, "expected string");
                const auto text = title.value.get<std::string>();
                if (text.size() > 128)
                    title.fail(Error::limit, "title exceeds 128 UTF-8 bytes");
                quests::QuestDefinition d{id, positive<std::uint32_t>(row.at("version")), positive<std::uint32_t>(row.at("minimum_level")), {}, reward(row.at("reward")), quests::CompletionPolicy::all, text};
                const auto goals = row.at("objectives");
                goals.array(quests::max_objectives);
                std::set<std::uint32_t> goal_ids;
                for (std::size_t j = 0; j < goals.value.size(); ++j)
                {
                    const auto goal = goals.at(j);
                    if (schema == 1)
                        goal.object({"id", "type", "rule_version", "target"});
                    else
                        goal.object({"id", "type", "rule_version", "target", "binding"});
                    const auto goal_id = positive<std::uint32_t>(goal.at("id"));
                    unique(goal_ids, goal_id, goal.at("id"));
                    quests::ObjectiveDefinition o{goal_id, positive<std::uint32_t>(goal.at("type")),
                                                  positive<std::uint32_t>(goal.at("rule_version")), positive<std::uint64_t>(goal.at("target"))};
                    if (schema >= 2)
                    {
                        const auto binding = goal.at("binding");
                        if (!binding.value.is_null())
                        {
                            binding.object({"fact_type", "subject", "input"});
                            const auto input = binding.at("input");
                            if (!input.value.is_string() || (input.value.get<std::string>() != "quantity" && input.value.get<std::string>() != "one"))
                                input.fail(Error::rule, "expected input quantity or one");
                            o.binding = quests::ObjectiveBinding{positive<std::uint32_t>(binding.at("fact_type")),
                                                                 positive<std::uint32_t>(binding.at("subject")), input.value.get<std::string>() == "one" ? quests::FactInput::one : quests::FactInput::quantity};
                        }
                    }
                    const auto *handler = registry.find(o.type);
                    if (!handler)
                        goal.at("type").fail(Error::rule, "unregistered objective type");
                    if (!handler->validate(o, 0))
                        goal.fail(Error::rule, "objective rule/version does not accept initial progress 0");
                    d.objectives.push_back(o);
                }
                domain(row, [&]
                       { const quests::QuestCatalog check({d}); });
                reward_references(row.at("reward"), items, currencies);
                quest_defs.push_back(std::move(d));
            }
        std::vector<interaction::Definition> interaction_defs;
        if (schema == 3) {
            const auto points = root.at("interactions"); points.array(interaction::max_points); ids.clear();
            for (std::size_t i = 0; i < points.value.size(); ++i) {
                auto row = points.record(i); row.object({"id", "version", "enabled", "mode", "fact_type"});
                const auto id = positive<std::uint32_t>(row.at("id")); row.record_id = id; unique(ids, id, row.at("id"));
                const auto enabled = row.at("enabled");
                if (!enabled.value.is_boolean()) enabled.fail(Error::structure, "expected boolean");
                const auto mode = row.at("mode");
                if (!mode.value.is_string() || (mode.value.get<std::string>() != "once" && mode.value.get<std::string>() != "repeatable"))
                    mode.fail(Error::rule, "expected mode once or repeatable");
                interaction_defs.push_back({id, positive<std::uint32_t>(row.at("version")), enabled.value.get<bool>(),
                    mode.value.get<std::string>() == "once" ? interaction::Mode::once : interaction::Mode::repeatable,
                    positive<std::uint32_t>(row.at("fact_type"))});
            }
        }
            // 唯一发布点：调用方只能获得校验完成的一整套配置，不存在部分成功。
            return {std::unique_ptr<const Bundle>(new Bundle(version, std::move(items), std::move(currencies),
                                                             commerce::ProductCatalog(std::move(product_defs)), quests::QuestCatalog(std::move(quest_defs)), registry, interaction::Catalog(std::move(interaction_defs)))),
                    std::nullopt};
        }
        catch (const Failure &failure)
        {
            return {nullptr, failure.diagnostic};
        }
    }
    LoadResult load(const std::filesystem::path &path, const quests::ObjectiveRegistry &registry)
    {
        std::string bytes;
        const auto error = persistence::load_file(path, bytes, max_bytes);
        if (error != persistence::FileError::none)
            return {nullptr, Diagnostic{error == persistence::FileError::too_large ? Error::limit : Error::io,
                                        path.string(),
                                        "$",
                                        {},
                                        persistence::file_error_name(error)}};
        return decode(bytes, registry, path.string());
    }
} // namespace designer::configuration
