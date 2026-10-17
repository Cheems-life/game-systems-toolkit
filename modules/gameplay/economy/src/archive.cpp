#include <designer/economy/archive.hpp>

#include <set>
#include <utility>
#include <type_traits>

namespace designer::economy {
static_assert(std::is_nothrow_move_assignable<Snapshot>::value, "Decoded output commit must not throw");
namespace {
constexpr std::string_view magic = "DECOARCH";
constexpr std::uint32_t version = 1;
std::uint64_t checksum(std::string_view bytes) {
    std::uint64_t result = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) { result ^= byte; result *= 1099511628211ULL; }
    return result;
}
void append(std::string& bytes, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<char>((value >> (i * 8)) & 255));
}
class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}
    bool read(std::uint64_t& value, unsigned count) {
        if (count > bytes_.size() - offset_) return false;
        value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(static_cast<unsigned char>(bytes_[offset_++])) << (i * 8);
        return true;
    }
    bool slice(std::size_t size, std::string_view& output) {
        if (size > bytes_.size() - offset_) return false;
        output = bytes_.substr(offset_, size);
        offset_ += size;
        return true;
    }
    bool finished() const { return offset_ == bytes_.size(); }
private:
    std::string_view bytes_;
    std::size_t offset_ = 0;
};
bool currencies_valid(const std::vector<CurrencyAmount>& values) {
    if (values.size() > max_entries_per_list) return false;
    CurrencyId previous = 0;
    for (const auto& value : values) {
        if (value.currency <= previous || value.amount == 0) return false;
        previous = value.currency;
    }
    return true;
}
bool history_valid(const Snapshot& snapshot) {
    std::set<RequestId> ids;
    std::set<ContainerId> containers;
    for (const auto& container : snapshot.containers) containers.insert(container.id);
    for (std::size_t i = 0; i < snapshot.receipts.size(); ++i) {
        const auto& receipt = snapshot.receipts[i];
        const auto& request = receipt.transaction;
        if (receipt.sequence != i + 1 || request.request_id == 0 || request.destination == 0 ||
            !ids.insert(request.request_id).second || !containers.count(request.destination) ||
            !currencies_valid(request.cost.currencies) || !currencies_valid(request.reward.currencies) ||
            request.reward.items.size() > max_entries_per_list ||
            (request.cost.currencies.empty() && request.reward.currencies.empty() && request.reward.items.empty())) return false;
        inventory::ItemId previous = 0;
        for (const auto& item : request.reward.items) {
            if (item.item <= previous || item.quantity == 0) return false;
            previous = item.item;
        }
    }
    return true;
}
void write_currencies(std::string& output, const std::vector<CurrencyAmount>& values) {
    append(output, values.size(), 4);
    for (const auto& value : values) { append(output, value.currency, 4); append(output, value.amount, 8); }
}
bool read_currencies(Reader& reader, std::vector<CurrencyAmount>& values) {
    std::uint64_t count = 0;
    if (!reader.read(count, 4) || count > max_entries_per_list) return false;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t id = 0, amount = 0;
        if (!reader.read(id, 4) || !reader.read(amount, 8)) return false;
        values.push_back({static_cast<CurrencyId>(id), amount});
    }
    return currencies_valid(values);
}
} // namespace
const char* archive_error_name(ArchiveError error) noexcept {
    switch (error) {
    case ArchiveError::none: return "none";
    case ArchiveError::too_large: return "too_large";
    case ArchiveError::malformed: return "malformed";
    case ArchiveError::unsupported_version: return "unsupported_version";
    case ArchiveError::checksum_mismatch: return "checksum_mismatch";
    }
    return "unrecognized_error";
}
ArchiveError encode_archive(const Snapshot& snapshot, std::string& output) {
    if (snapshot.balances.size() > max_currencies || snapshot.receipts.size() > max_history ||
        snapshot.containers.size() > inventory::archive_max_containers) return ArchiveError::too_large;
    for (const auto& entry : snapshot.balances) if (entry.first == 0 || entry.second == 0) return ArchiveError::malformed;
    if (!history_valid(snapshot)) return ArchiveError::malformed;
    std::string containers;
    const auto nested = inventory::encode_archive(snapshot.containers, containers);
    if (nested.error == inventory::ArchiveError::too_large) return ArchiveError::too_large;
    if (nested.error != inventory::ArchiveError::none) return ArchiveError::malformed;
    std::string candidate(magic);
    append(candidate, version, 4);
    append(candidate, snapshot.balances.size(), 4);
    for (const auto& entry : snapshot.balances) { append(candidate, entry.first, 4); append(candidate, entry.second, 8); }
    append(candidate, containers.size(), 4);
    candidate += containers;
    append(candidate, snapshot.receipts.size(), 4);
    for (const auto& receipt : snapshot.receipts) {
        append(candidate, receipt.sequence, 8);
        append(candidate, receipt.transaction.request_id, 8);
        append(candidate, receipt.transaction.destination, 4);
        write_currencies(candidate, receipt.transaction.cost.currencies);
        write_currencies(candidate, receipt.transaction.reward.currencies);
        append(candidate, receipt.transaction.reward.items.size(), 4);
        for (const auto& item : receipt.transaction.reward.items) {
            append(candidate, item.item, 4); append(candidate, item.quantity, 4);
        }
    }
    append(candidate, checksum(candidate), 8);
    if (candidate.size() > archive_max_bytes) return ArchiveError::too_large;
    output.swap(candidate);
    return ArchiveError::none;
}
ArchiveError decode_archive(std::string_view bytes, Snapshot& output) {
    if (bytes.size() > archive_max_bytes) return ArchiveError::too_large;
    if (bytes.size() < 28 || bytes.substr(0, 8) != magic) return ArchiveError::malformed;
    Reader header(bytes.substr(8, 4));
    std::uint64_t file_version = 0;
    if (!header.read(file_version, 4)) return ArchiveError::malformed;
    if (file_version != version) return ArchiveError::unsupported_version;
    Reader footer(bytes.substr(bytes.size() - 8));
    std::uint64_t stored_checksum = 0;
    if (!footer.read(stored_checksum, 8)) return ArchiveError::malformed;
    if (stored_checksum != checksum(bytes.substr(0, bytes.size() - 8))) return ArchiveError::checksum_mismatch;
    Reader reader(bytes.substr(12, bytes.size() - 20));
    Snapshot candidate;
    std::uint64_t count = 0;
    if (!reader.read(count, 4)) return ArchiveError::malformed;
    if (count > max_currencies) return ArchiveError::too_large;
    CurrencyId previous = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t id = 0, amount = 0;
        if (!reader.read(id, 4) || !reader.read(amount, 8) || id <= previous || amount == 0) return ArchiveError::malformed;
        previous = static_cast<CurrencyId>(id);
        candidate.balances.emplace(static_cast<CurrencyId>(id), amount);
    }
    std::uint64_t length = 0;
    if (!reader.read(length, 4)) return ArchiveError::malformed;
    if (length > inventory::archive_max_bytes) return ArchiveError::too_large;
    std::string_view containers;
    if (!reader.slice(static_cast<std::size_t>(length), containers)) return ArchiveError::malformed;
    const auto nested = inventory::decode_archive(containers, candidate.containers);
    if (nested.error == inventory::ArchiveError::too_large) return ArchiveError::too_large;
    if (nested.error != inventory::ArchiveError::none) return ArchiveError::malformed;
    if (!reader.read(count, 4)) return ArchiveError::malformed;
    if (count > max_history) return ArchiveError::too_large;
    candidate.receipts.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        Receipt receipt{};
        std::uint64_t destination = 0;
        if (!reader.read(receipt.sequence, 8) || !reader.read(receipt.transaction.request_id, 8) ||
            !reader.read(destination, 4)) return ArchiveError::malformed;
        receipt.transaction.destination = static_cast<ContainerId>(destination);
        if (!read_currencies(reader, receipt.transaction.cost.currencies) ||
            !read_currencies(reader, receipt.transaction.reward.currencies)) return ArchiveError::malformed;
        std::uint64_t items = 0;
        if (!reader.read(items, 4) || items > max_entries_per_list) return ArchiveError::malformed;
        for (std::uint64_t j = 0; j < items; ++j) {
            std::uint64_t id = 0, quantity = 0;
            if (!reader.read(id, 4) || !reader.read(quantity, 4)) return ArchiveError::malformed;
            receipt.transaction.reward.items.push_back({static_cast<inventory::ItemId>(id), static_cast<inventory::Quantity>(quantity)});
        }
        candidate.receipts.push_back(std::move(receipt));
    }
    if (!reader.finished() || !history_valid(candidate)) return ArchiveError::malformed;
    output = std::move(candidate);
    return ArchiveError::none;
}

} // namespace designer::economy
