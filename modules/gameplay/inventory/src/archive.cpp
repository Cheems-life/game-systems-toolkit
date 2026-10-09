#include <designer/inventory/archive.hpp>
#include <unordered_set>
#include <utility>

namespace designer::inventory {
namespace {
constexpr std::string_view magic = "DINVARCH";
constexpr std::uint32_t version = 1;
std::uint64_t checksum(std::string_view bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
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
        for (unsigned i = 0; i < count; ++i) {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes_[offset_++])) << (i * 8);
        }
        return true;
    }
    bool finished() const { return offset_ == bytes_.size(); }
private:
    std::string_view bytes_;
    std::size_t offset_ = 0;
};
ArchiveResult validate(const Archive& archive) {
    if (archive.size() > archive_max_containers) return {ArchiveError::too_large};
    std::size_t total = 0;
    std::unordered_set<ContainerId> ids;
    for (const auto& container : archive) {
        if (container.id == 0) return {ArchiveError::invalid_container};
        if (!ids.insert(container.id).second) return {ArchiveError::duplicate_container};
        if (container.slots.size() > archive_max_slots_per_container ||
            container.slots.size() > archive_max_total_slots - total) return {ArchiveError::too_large};
        total += container.slots.size();
        for (const auto& slot : container.slots) {
            if (slot && (slot->item_id == 0 || slot->quantity == 0)) return {ArchiveError::malformed};
        }
    }
    return {};
}
} // namespace

const char* archive_error_name(ArchiveError error) noexcept {
    switch (error) {
    case ArchiveError::none: return "none";
    case ArchiveError::too_large: return "too_large";
    case ArchiveError::malformed: return "malformed";
    case ArchiveError::unsupported_version: return "unsupported_version";
    case ArchiveError::checksum_mismatch: return "checksum_mismatch";
    case ArchiveError::duplicate_container: return "duplicate_container";
    case ArchiveError::invalid_container: return "invalid_container";
    case ArchiveError::binding_mismatch: return "binding_mismatch";
    case ArchiveError::invalid_inventory: return "invalid_inventory";
    }
    return "unrecognized_error";
}

ArchiveResult encode_archive(const Archive& archive, std::string& output) {
    const auto checked = validate(archive);
    if (checked.error != ArchiveError::none) return checked;
    std::string candidate(magic);
    append(candidate, version, 4);
    append(candidate, archive.size(), 4);
    for (const auto& container : archive) {
        append(candidate, container.id, 4);
        append(candidate, container.slots.size(), 4);
        for (const auto& slot : container.slots) {
            append(candidate, slot ? 1 : 0, 1);
            if (slot) {
                append(candidate, slot->item_id, 4);
                append(candidate, slot->quantity, 4);
            }
        }
    }
    append(candidate, checksum(candidate), 8);
    if (candidate.size() > archive_max_bytes) return {ArchiveError::too_large};
    output.swap(candidate);
    return {};
}

ArchiveResult decode_archive(std::string_view bytes, Archive& output) {
    if (bytes.size() > archive_max_bytes) return {ArchiveError::too_large};
    if (bytes.size() < 24 || bytes.substr(0, 8) != magic) return {ArchiveError::malformed};
    Reader header(bytes.substr(8, 8));
    std::uint64_t file_version = 0, count = 0;
    if (!header.read(file_version, 4) || !header.read(count, 4)) return {ArchiveError::malformed};
    if (file_version != version) return {ArchiveError::unsupported_version};
    std::uint64_t stored_checksum = 0;
    Reader footer(bytes.substr(bytes.size() - 8));
    if (!footer.read(stored_checksum, 8)) return {ArchiveError::malformed};
    if (stored_checksum != checksum(bytes.substr(0, bytes.size() - 8))) return {ArchiveError::checksum_mismatch};
    if (count > archive_max_containers) return {ArchiveError::too_large};
    Reader reader(bytes.substr(16, bytes.size() - 24));
    Archive candidate;
    candidate.reserve(static_cast<std::size_t>(count));
    std::size_t total = 0;
    std::unordered_set<ContainerId> ids;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t id = 0, slots = 0;
        if (!reader.read(id, 4) || !reader.read(slots, 4)) return {ArchiveError::malformed};
        if (id == 0) return {ArchiveError::invalid_container};
        if (!ids.insert(static_cast<ContainerId>(id)).second) return {ArchiveError::duplicate_container};
        if (slots > archive_max_slots_per_container || slots > archive_max_total_slots - total)
            return {ArchiveError::too_large};
        total += static_cast<std::size_t>(slots);
        ContainerSnapshot container{static_cast<ContainerId>(id), Snapshot(static_cast<std::size_t>(slots))};
        for (auto& slot : container.slots) {
            std::uint64_t tag = 0, item = 0, quantity = 0;
            if (!reader.read(tag, 1) || tag > 1) return {ArchiveError::malformed};
            if (tag == 1) {
                if (!reader.read(item, 4) || !reader.read(quantity, 4) || item == 0 || quantity == 0)
                    return {ArchiveError::malformed};
                slot = ItemStack{static_cast<ItemId>(item), static_cast<Quantity>(quantity)};
            }
        }
        candidate.push_back(std::move(container));
    }
    if (!reader.finished()) return {ArchiveError::malformed};
    output.swap(candidate);
    return {};
}

ArchiveResult restore_archive(const Archive& archive, const std::vector<ContainerBinding>& bindings) {
    const auto checked = validate(archive);
    if (checked.error != ArchiveError::none) return checked;
    if (archive.size() != bindings.size()) return {ArchiveError::binding_mismatch};
    std::unordered_set<ContainerId> ids;
    std::unordered_set<Inventory*> targets;
    std::vector<RestoreRequest> requests;
    requests.reserve(bindings.size());
    for (const auto& binding : bindings) {
        if (binding.id == 0 || !binding.inventory || !ids.insert(binding.id).second ||
            !targets.insert(binding.inventory).second) return {ArchiveError::binding_mismatch};
        const Snapshot* snapshot = nullptr;
        for (const auto& container : archive) if (container.id == binding.id) snapshot = &container.slots;
        if (!snapshot) return {ArchiveError::binding_mismatch};
        requests.push_back({binding.inventory, snapshot});
    }
    const auto result = Inventory::restore_many(requests);
    if (result != Error::none) return {ArchiveError::invalid_inventory, result};
    return {};
}

} // namespace designer::inventory
