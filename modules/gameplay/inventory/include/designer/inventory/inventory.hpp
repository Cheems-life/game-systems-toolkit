#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace designer::inventory {

using ItemId = std::uint32_t;
using Quantity = std::uint32_t;

struct ItemDefinition {
    ItemId id;
    Quantity max_stack;
};

struct ItemStack {
    ItemId item_id;
    Quantity quantity;
};

inline bool operator==(const ItemStack& lhs, const ItemStack& rhs) noexcept {
    return lhs.item_id == rhs.item_id && lhs.quantity == rhs.quantity;
}
using Slot = std::optional<ItemStack>;      // 可能有物品，也可能没有
using Snapshot = std::vector<Slot>;

class Inventory;
struct RestoreRequest {
    Inventory* target;
    const Snapshot* snapshot;
};

// Immutable rules. Invalid IDs, zero limits and duplicate IDs throw invalid_argument.
class ItemCatalog {
public:
    explicit ItemCatalog(std::vector<ItemDefinition> definitions);
    [[nodiscard]] const ItemDefinition* find(ItemId id) const noexcept;
private:
    std::unordered_map<ItemId, ItemDefinition> definitions_;
};

enum class Error {
    none,
    invalid_quantity,
    unknown_item,
    invalid_slot,
    empty_source,
    insufficient_quantity,
    insufficient_capacity,
    incompatible_item,
    same_container,
    invalid_snapshot,
    slot_count_mismatch,
    invalid_stack
};
[[nodiscard]] const char* error_name(Error error) noexcept;

// Single-threaded, fixed-size container. All reported failures leave every slot unchanged.
// Configuration and allocation errors use exceptions; business failures use Error.
class Inventory {
public:
    Inventory(std::size_t slot_count, ItemCatalog catalog);
    [[nodiscard]] const std::vector<Slot>& slots() const noexcept { return slots_; }
    [[nodiscard]] Error add(ItemId item, Quantity quantity);
    [[nodiscard]] Error remove(ItemId item, Quantity quantity);
    [[nodiscard]] Error move(std::size_t source, std::size_t destination, Quantity quantity);
    [[nodiscard]] Error swap_slots(std::size_t first, std::size_t second);
    [[nodiscard]] Error transfer_to(Inventory& target, ItemId item, Quantity quantity);
    [[nodiscard]] Error transfer_all_to(Inventory& target);
    [[nodiscard]] Snapshot snapshot() const { return slots_; }
    [[nodiscard]] Error restore(const Snapshot& snapshot);
    // Validate and stage ALL snapshots before committing any container.
    [[nodiscard]] static Error restore_many(const std::vector<RestoreRequest>& requests);

private:
    [[nodiscard]] Error validate_snapshot(const Snapshot& snapshot) const;
    ItemCatalog catalog_;
    std::vector<Slot> slots_;
};

} // namespace designer::inventory
