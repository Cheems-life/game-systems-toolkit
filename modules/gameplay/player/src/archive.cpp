#include <utility>
#include <designer/player/archive.hpp>
#include <designer/commerce/archive.hpp>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace designer::player
{
    static_assert(std::is_nothrow_move_assignable<PlayerSnapshot>::value, "Archive commit must not allocate");
    namespace
    {
        constexpr std::string_view magic = "DQSTARCH";
        std::uint64_t checksum(std::string_view bytes)
        {
            std::uint64_t hash = 14695981039346656037ULL;
            for (const unsigned char byte : bytes)
            {
                hash ^= byte;
                hash *= 1099511628211ULL;
            }
            return hash;
        }
        void put(std::string &bytes, std::uint64_t value, unsigned size)
        {
            for (unsigned i = 0; i < size; ++i)
                bytes.push_back(static_cast<char>((value >> (i * 8)) & 255));
        }
        class Reader
        {
        public:
            explicit Reader(std::string_view bytes) : bytes_(bytes) {}
            bool get(std::uint64_t &value, unsigned size)
            {
                if (size > bytes_.size() - offset_)
                    return false;
                value = 0;
                for (unsigned i = 0; i < size; ++i)
                    value |= std::uint64_t(static_cast<unsigned char>(bytes_[offset_++])) << (i * 8);
                return true;
            }
            bool u32(std::uint32_t &value)
            {
                std::uint64_t raw = 0;
                if (!get(raw, 4))
                    return false;
                value = static_cast<std::uint32_t>(raw);
                return true;
            }
            bool count(std::size_t maximum, std::size_t &value)
            {
                std::uint64_t raw = 0;
                if (!get(raw, 4) || raw > maximum)
                    return false;
                value = static_cast<std::size_t>(raw);
                return true;
            }
            bool slice(std::size_t size, std::string_view &value)
            {
                if (size > bytes_.size() - offset_)
                    return false;
                value = bytes_.substr(offset_, size);
                offset_ += size;
                return true;
            }
            bool finished() const noexcept { return offset_ == bytes_.size(); }

        private:
            std::string_view bytes_;
            std::size_t offset_ = 0;
        };
        bool less_update(const quests::ObjectiveUpdate &a, const quests::ObjectiveUpdate &b)
        {
            return a.quest < b.quest || (a.quest == b.quest && a.objective < b.objective);
        }
        bool valid(const PlayerSnapshot &snapshot)
        {
            const auto &book = snapshot.quests;
            if (book.quests.size() > quests::max_quests || book.events.size() > quests::max_events || book.claims.size() > quests::max_quests || snapshot.interactions.size() > interaction::max_history)
                return false;
            std::map<quests::QuestId, const quests::AcceptedQuest *> quests;
            quests::QuestId previous = 0;
            for (const auto &quest : book.quests)
            {
                const auto &d = quest.definition;
                if (d.id <= previous || quest.progress.size() != d.objectives.size())
                    return false;
                previous = d.id;
                try
                {
                    quests::QuestCatalog catalog({d});
                    if (!(*catalog.find(d.id) == d))
                        return false;
                }
                catch (const std::invalid_argument &)
                {
                    return false;
                }
                bool bound = false;
                for (const auto& objective : d.objectives) if (objective.binding) bound = true;
                if ((!quest.claimed || !bound) && quest.fact_until != 0) return false;
                for (std::size_t i = 0; i < quest.progress.size(); ++i)
                    if (quest.progress[i] > d.objectives[i].target)
                        return false;
                quests.emplace(d.id, &quest);
            }
            quests::EventId event_id = 0;
            for (const auto &event : book.events)
            {
                if (event.id <= event_id || event.updates.size() > quests::max_updates ||
                    (event.fact && (!quests::valid_fact(*event.fact) || event.fact->id != event.id)))
                    return false;
                event_id = event.id;
                for (std::size_t i = 0; i < event.updates.size(); ++i)
                {
                    const auto &update = event.updates[i];
                    const auto found = quests.find(update.quest);
                    if (update.value == 0 || update.objective == 0 || found == quests.end() || event.id <= found->second->accepted_after ||
                        (i > 0 && !less_update(event.updates[i - 1], update)))
                        return false;
                    bool known = false;
                    for (const auto &objective : found->second->definition.objectives)
                        if (objective.id == update.objective)
                            known = true;
                    if (!known)
                        return false;
                }
            }
            for (const auto& quest : book.quests) {
                if (quest.fact_until > event_id) return false;
                if (quest.fact_until != 0 && std::none_of(book.events.begin(), book.events.end(),
                    [&](const auto& e) { return e.id == quest.fact_until; })) return false;
            }
            interaction::Book interaction_validator(interaction::max_history);
            if (interaction_validator.restore(snapshot.interactions) != interaction::Error::none) return false;
            for (const auto& receipt : snapshot.interactions) {
                const auto found = std::lower_bound(book.events.begin(), book.events.end(), receipt.event_id,
                    [](const auto& event, quests::EventId id) { return event.id < id; });
                const quests::GameplayFact expected{receipt.event_id, receipt.fact_type, receipt.request.point, 1};
                if (found == book.events.end() || found->id != receipt.event_id || !found->fact || !(*found->fact == expected)) return false;
            }
            std::map<economy::RequestId, const economy::Receipt *> ledger;
            std::set<economy::RequestId> purchases, requests;
            std::set<quests::QuestId> claimed;
            for (const auto &receipt : snapshot.commerce.economy.receipts)
                ledger.emplace(receipt.transaction.request_id, &receipt);
            for (const auto &purchase : snapshot.commerce.purchases)
                purchases.insert(purchase.request.request_id);
            std::uint64_t sequence = 0;
            for (const auto &claim : book.claims)
            {
                const auto &request = claim.request;
                const auto found = quests.find(request.quest);
                const auto record = ledger.find(request.request_id);
                const auto &transaction = claim.settlement.transaction;
                if (request.request_id == 0 || request.destination == 0 || found == quests.end() || !found->second->claimed ||
                    !claimed.insert(request.quest).second || !requests.insert(request.request_id).second || purchases.count(request.request_id) ||
                    record == ledger.end() || !(*record->second == claim.settlement) || claim.settlement.sequence <= sequence ||
                    transaction.request_id != request.request_id || transaction.destination != request.destination ||
                    !transaction.cost.currencies.empty() || transaction.reward.currencies != found->second->definition.reward.currencies ||
                    transaction.reward.items != found->second->definition.reward.items)
                    return false;
                sequence = claim.settlement.sequence;
            }
            for (const auto &entry : quests)
                if (entry.second->claimed != (claimed.count(entry.first) != 0))
                    return false;
            return true;
        }
        void write_reward(std::string &bytes, const economy::RewardBundle &reward)
        {
            put(bytes, reward.currencies.size(), 4);
            for (const auto &v : reward.currencies)
            {
                put(bytes, v.currency, 4);
                put(bytes, v.amount, 8);
            }
            put(bytes, reward.items.size(), 4);
            for (const auto &v : reward.items)
            {
                put(bytes, v.item, 4);
                put(bytes, v.quantity, 4);
            }
        }
        bool read_reward(Reader &reader, economy::RewardBundle &reward)
        {
            std::size_t count = 0;
            if (!reader.count(economy::max_entries_per_list, count))
                return false;
            for (std::size_t i = 0; i < count; ++i)
            {
                economy::CurrencyAmount v{};
                if (!reader.u32(v.currency) || !reader.get(v.amount, 8))
                    return false;
                reward.currencies.push_back(v);
            }
            if (!reader.count(economy::max_entries_per_list, count))
                return false;
            for (std::size_t i = 0; i < count; ++i)
            {
                economy::ItemAmount v{};
                if (!reader.u32(v.item) || !reader.u32(v.quantity))
                    return false;
                reward.items.push_back(v);
            }
            return true;
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
    ArchiveError encode_archive(const PlayerSnapshot &snapshot, std::string &output)
    {
        if (snapshot.quests.quests.size() > quests::max_quests || snapshot.quests.events.size() > quests::max_events || snapshot.quests.claims.size() > quests::max_quests || snapshot.interactions.size() > interaction::max_history)
            return ArchiveError::too_large;
        if (!valid(snapshot))
            return ArchiveError::malformed;
        std::string nested;
        const auto encoded = commerce::encode_archive(snapshot.commerce, nested);
        if (encoded == commerce::ArchiveError::too_large)
            return ArchiveError::too_large;
        if (encoded != commerce::ArchiveError::none)
            return ArchiveError::malformed;
        std::string bytes(magic);
        put(bytes, 3, 4);
        put(bytes, nested.size(), 4);
        bytes += nested;
        put(bytes, snapshot.quests.quests.size(), 4);
        for (const auto &quest : snapshot.quests.quests)
        {
            const auto &d = quest.definition;
            put(bytes, d.id, 4);
            put(bytes, d.version, 4);
            put(bytes, d.minimum_level, 4);
            put(bytes, static_cast<std::uint32_t>(d.policy), 4);
            put(bytes, d.title.size(), 4);
            bytes += d.title;
            put(bytes, quest.accepted_after, 8);
            put(bytes, quest.claimed ? 1 : 0, 1);
            put(bytes, quest.fact_until, 8);
            put(bytes, d.objectives.size(), 4);
            for (std::size_t i = 0; i < d.objectives.size(); ++i)
            {
                const auto &objective = d.objectives[i];
                put(bytes, objective.id, 4);
                put(bytes, objective.type, 4);
                put(bytes, objective.rule_version, 4);
                put(bytes, objective.target, 8);
                put(bytes, quest.progress[i], 8);
                put(bytes, objective.binding ? 1 : 0, 1);
                if (objective.binding) {
                    put(bytes, objective.binding->fact_type, 4);
                    put(bytes, objective.binding->subject, 4);
                    put(bytes, static_cast<std::uint32_t>(objective.binding->input), 4);
                }
            }
            write_reward(bytes, d.reward);
        }
        put(bytes, snapshot.quests.events.size(), 4);
        for (const auto &event : snapshot.quests.events)
        {
            put(bytes, event.id, 8);
            put(bytes, event.updates.size(), 4);
            for (const auto &update : event.updates)
            {
                put(bytes, update.quest, 4);
                put(bytes, update.objective, 4);
                put(bytes, update.value, 8);
            }
            put(bytes, event.fact ? 1 : 0, 1);
            if (event.fact) {
                put(bytes, event.fact->type, 4);
                put(bytes, event.fact->subject, 4);
                put(bytes, event.fact->quantity, 8);
            }
        }
        put(bytes, snapshot.quests.claims.size(), 4);
        for (const auto &claim : snapshot.quests.claims)
        {
            put(bytes, claim.request.request_id, 8);
            put(bytes, claim.request.quest, 4);
            put(bytes, claim.request.destination, 4);
            put(bytes, claim.settlement.sequence, 8);
        }
        put(bytes, snapshot.interactions.size(), 4);
        for (const auto& receipt : snapshot.interactions) {
            put(bytes, receipt.request.request_id, 8);
            put(bytes, receipt.request.point, 4);
            put(bytes, receipt.version, 4);
            put(bytes, static_cast<std::uint32_t>(receipt.mode), 4);
            put(bytes, receipt.fact_type, 4);
            put(bytes, receipt.event_id, 8);
        }
        put(bytes, checksum(bytes), 8);
        if (bytes.size() > archive_max_bytes)
            return ArchiveError::too_large;
        output.swap(bytes);
        return ArchiveError::none;
    }
    ArchiveError decode_archive(std::string_view bytes, PlayerSnapshot &output)
    {
        if (bytes.size() > archive_max_bytes)
            return ArchiveError::too_large;
        if (bytes.size() < 36 || bytes.substr(0, 8) != magic)
            return ArchiveError::malformed;
        Reader header(bytes.substr(8, 8));
        std::uint64_t version = 0, size = 0;
        if (!header.get(version, 4) || !header.get(size, 4))
            return ArchiveError::malformed;
        if (version != 1 && version != 2 && version != 3)
            return ArchiveError::unsupported_version;
        Reader footer(bytes.substr(bytes.size() - 8));
        std::uint64_t hash = 0;
        if (!footer.get(hash, 8) || hash != checksum(bytes.substr(0, bytes.size() - 8)))
            return ArchiveError::checksum_mismatch;
        if (size > commerce::archive_max_bytes)
            return ArchiveError::too_large;
        Reader reader(bytes.substr(16, bytes.size() - 24));
        std::string_view nested;
        if (!reader.slice(static_cast<std::size_t>(size), nested))
            return ArchiveError::malformed;
        PlayerSnapshot candidate;
        const auto decoded = commerce::decode_archive(nested, candidate.commerce);
        if (decoded == commerce::ArchiveError::too_large)
            return ArchiveError::too_large;
        if (decoded != commerce::ArchiveError::none)
            return ArchiveError::malformed;
        std::size_t count = 0;
        if (!reader.count(quests::max_quests, count))
            return ArchiveError::malformed;
        for (std::size_t i = 0; i < count; ++i)
        {
            quests::AcceptedQuest quest{};
            auto &d = quest.definition;
            std::uint32_t policy = 0;
            std::size_t title_size = 0;
            std::string_view title;
            std::uint64_t claimed = 0;
            if (!reader.u32(d.id) || !reader.u32(d.version) || !reader.u32(d.minimum_level) || !reader.u32(policy) ||
                !reader.count(128, title_size) || !reader.slice(title_size, title) || !reader.get(quest.accepted_after, 8) ||
                !reader.get(claimed, 1) || claimed > 1)
                return ArchiveError::malformed;
            d.policy = static_cast<quests::CompletionPolicy>(policy);
            d.title = std::string(title);
            quest.claimed = claimed == 1;
            if (version >= 2 && !reader.get(quest.fact_until, 8)) return ArchiveError::malformed;
            std::size_t objectives = 0;
            if (!reader.count(quests::max_objectives, objectives))
                return ArchiveError::malformed;
            for (std::size_t j = 0; j < objectives; ++j)
            {
                quests::ObjectiveDefinition definition{};
                std::uint64_t progress = 0;
                if (!reader.u32(definition.id) || !reader.u32(definition.type) || !reader.u32(definition.rule_version) ||
                    !reader.get(definition.target, 8) || !reader.get(progress, 8))
                    return ArchiveError::malformed;
                if (version >= 2) {
                    std::uint64_t present = 0;
                    if (!reader.get(present, 1) || present > 1) return ArchiveError::malformed;
                    if (present == 1) {
                        quests::ObjectiveBinding binding{};
                        std::uint32_t input = 0;
                        if (!reader.u32(binding.fact_type) || !reader.u32(binding.subject) || !reader.u32(input))
                            return ArchiveError::malformed;
                        binding.input = static_cast<quests::FactInput>(input);
                        definition.binding = binding;
                    }
                }
                d.objectives.push_back(definition);
                quest.progress.push_back(progress);
            }
            if (!read_reward(reader, d.reward))
                return ArchiveError::malformed;
            candidate.quests.quests.push_back(std::move(quest));
        }
        if (!reader.count(quests::max_events, count))
            return ArchiveError::malformed;
        for (std::size_t i = 0; i < count; ++i)
        {
            quests::ProgressEvent event{};
            std::size_t updates = 0;
            if (!reader.get(event.id, 8) || !reader.count(quests::max_updates, updates))
                return ArchiveError::malformed;
            for (std::size_t j = 0; j < updates; ++j)
            {
                quests::ObjectiveUpdate update{};
                if (!reader.u32(update.quest) || !reader.u32(update.objective) || !reader.get(update.value, 8))
                    return ArchiveError::malformed;
                event.updates.push_back(update);
            }
            if (version >= 2) {
                std::uint64_t present = 0;
                if (!reader.get(present, 1) || present > 1) return ArchiveError::malformed;
                if (present == 1) {
                    quests::GameplayFact fact{event.id, 0, 0, 0};
                    if (!reader.u32(fact.type) || !reader.u32(fact.subject) || !reader.get(fact.quantity, 8))
                        return ArchiveError::malformed;
                    event.fact = fact;
                }
            }
            candidate.quests.events.push_back(std::move(event));
        }
        if (!reader.count(quests::max_quests, count))
            return ArchiveError::malformed;
        std::map<economy::RequestId, const economy::Receipt *> receipts;
        for (const auto &receipt : candidate.commerce.economy.receipts)
            receipts.emplace(receipt.transaction.request_id, &receipt);
        for (std::size_t i = 0; i < count; ++i)
        {
            quests::ClaimRequest request{};
            std::uint64_t sequence = 0;
            if (!reader.get(request.request_id, 8) || !reader.u32(request.quest) || !reader.u32(request.destination) ||
                !reader.get(sequence, 8))
                return ArchiveError::malformed;
            const auto found = receipts.find(request.request_id);
            if (found == receipts.end() || found->second->sequence != sequence)
                return ArchiveError::malformed;
            candidate.quests.claims.push_back({request, *found->second});
        }
        if (version == 3) {
            if (!reader.count(interaction::max_history, count)) return ArchiveError::malformed;
            for (std::size_t i = 0; i < count; ++i) {
                interaction::Receipt receipt{}; std::uint32_t mode = 0;
                if (!reader.get(receipt.request.request_id, 8) || !reader.u32(receipt.request.point) || !reader.u32(receipt.version) ||
                    !reader.u32(mode) || !reader.u32(receipt.fact_type) || !reader.get(receipt.event_id, 8)) return ArchiveError::malformed;
                receipt.mode = static_cast<interaction::Mode>(mode); candidate.interactions.push_back(receipt);
            }
        }
        if (!reader.finished() || !valid(candidate))
            return ArchiveError::malformed;
        output = std::move(candidate);
        return ArchiveError::none;
    }
} // namespace designer::player
