#include <designer/inventory/inventory.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace designer::inventory {

ItemCatalog::ItemCatalog(std::vector<ItemDefinition> definitions) {
    for (const auto& definition : definitions) {
        if (definition.id == 0 || definition.max_stack == 0) {
            throw std::invalid_argument("Item ID and stack limit must be positive");
        }
        if (!definitions_.emplace(definition.id, definition).second) {
            throw std::invalid_argument("Duplicate item ID");
        }
    }
}

const ItemDefinition* ItemCatalog::find(ItemId id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

const char* error_name(Error error) noexcept {
    switch (error) {
    case Error::none: return "none";
    case Error::invalid_quantity: return "invalid_quantity";
    case Error::unknown_item: return "unknown_item";
    case Error::invalid_slot: return "invalid_slot";
    case Error::empty_source: return "empty_source";
    case Error::insufficient_quantity: return "insufficient_quantity";
    case Error::insufficient_capacity: return "insufficient_capacity";
    case Error::incompatible_item: return "incompatible_item";
    case Error::same_container: return "same_container";
    case Error::invalid_snapshot: return "invalid_snapshot";
    case Error::slot_count_mismatch: return "slot_count_mismatch";
    case Error::invalid_stack: return "invalid_stack";
    }
    return "unrecognized_error";
}

Inventory::Inventory(std::size_t slot_count, ItemCatalog catalog)
    : catalog_(std::move(catalog)), slots_(slot_count) {}

Error Inventory::add(ItemId item, Quantity quantity) {
    // 检查请求是否合法
    if (quantity == 0) return Error::invalid_quantity;
    const auto* definition = catalog_.find(item);
    if (!definition) return Error::unknown_item;

    // Subtract from remaining rather than summing capacities (which may overflow).
    // 只检查空间，暂时不修改背包
    auto remaining = quantity;
    for (const auto& slot : slots_) {
        // 整格可用、同类剩余空间、异类不能放
        const Quantity room = !slot ? definition->max_stack
            : slot->item_id == item ? definition->max_stack - slot->quantity : 0;
        remaining -= std::min(remaining, room);
        if (remaining == 0) break;
    }
    if (remaining != 0) return Error::insufficient_capacity;

    // 确认放得下后，实际写入
    remaining = quantity;
    for (auto& slot : slots_) {         // 先填已有同类堆
        if (slot && slot->item_id == item) {
            const auto amount = std::min(remaining, definition->max_stack - slot->quantity);
            slot->quantity += amount;
            remaining -= amount;
        }
        if (remaining == 0) return Error::none;
    }
    for (auto& slot : slots_) {     // 已有堆填完后，再使用空格
        if (!slot) {
            const auto amount = std::min(remaining, definition->max_stack);
            slot = ItemStack{item, amount};
            remaining -= amount;
        }
        if (remaining == 0) break;
    }
    return Error::none;
}

Error Inventory::remove(ItemId item, Quantity quantity) {
    // 检查参数
    if (quantity == 0) return Error::invalid_quantity;
    if (!catalog_.find(item)) return Error::unknown_item;

    // 检查整个背包是否有足够数量
    auto remaining = quantity;
    for (const auto& slot : slots_) {
        if (slot && slot->item_id == item) remaining -= std::min(remaining, slot->quantity);
        if (remaining == 0) break;
    }
    if (remaining != 0) return Error::insufficient_quantity;

    // 真正扣除
    remaining = quantity;
    for (auto& slot : slots_) {
        if (slot && slot->item_id == item) {
            const auto amount = std::min(remaining, slot->quantity);
            slot->quantity -= amount;
            remaining -= amount;
            if (slot->quantity == 0) slot.reset();
        }
        if (remaining == 0) break;
    }
    return Error::none;
}

// move(0, 2, 3)    从第 0 格向第 2 格移动 3 个
Error Inventory::move(std::size_t source, std::size_t destination, Quantity quantity) {
    if (quantity == 0) return Error::invalid_quantity;
    if (source >= slots_.size() || destination >= slots_.size()) return Error::invalid_slot;
    auto& from = slots_[source];
    auto& to = slots_[destination];
    if (!from) return Error::empty_source;
    if (quantity > from->quantity) return Error::insufficient_quantity;
    if (source == destination) return Error::none;
    if (to && to->item_id != from->item_id) return Error::incompatible_item;
    const auto max_stack = catalog_.find(from->item_id)->max_stack;
    if (to && quantity > max_stack - to->quantity) return Error::insufficient_capacity;

    if (to) to->quantity += quantity;
    else to = ItemStack{from->item_id, quantity};
    from->quantity -= quantity;
    if (from->quantity == 0) from.reset();
    return Error::none;
}

Error Inventory::swap_slots(std::size_t first, std::size_t second) {
    if (first >= slots_.size() || second >= slots_.size()) return Error::invalid_slot;
    if (first != second) std::swap(slots_[first], slots_[second]);
    return Error::none;
}

Error Inventory::transfer_to(Inventory& target, ItemId item, Quantity quantity) {
    if (this == &target) return Error::same_container;
    // Work on independent candidates: exceptions and failures preserve both originals.
    auto source_candidate = *this;
    auto target_candidate = target;
    const auto removed = source_candidate.remove(item, quantity);
    if (removed != Error::none) return removed;
    const auto added = target_candidate.add(item, quantity);
    if (added != Error::none) return added;
    slots_.swap(source_candidate.slots_);
    target.slots_.swap(target_candidate.slots_);
    return Error::none;
}

Error Inventory::transfer_all_to(Inventory& target) {
    if (this == &target) return Error::same_container;
    auto target_candidate = target;
    Snapshot empty(slots_.size());
    for (const auto& slot : slots_) {
        if (!slot) continue;
        const auto added = target_candidate.add(slot->item_id, slot->quantity);
        if (added != Error::none) return added;
    }
    slots_.swap(empty);
    target.slots_.swap(target_candidate.slots_);
    return Error::none;
}

Error Inventory::validate_snapshot(const Snapshot& snapshot) const {
    if (snapshot.size() != slots_.size()) return Error::slot_count_mismatch;
    for (const auto& slot : snapshot) {
        if (!slot) continue;
        const auto* definition = catalog_.find(slot->item_id);
        if (!definition) return Error::unknown_item;
        if (slot->quantity == 0 || slot->quantity > definition->max_stack) return Error::invalid_stack;
    }
    return Error::none;
}

Error Inventory::restore(const Snapshot& snapshot) {
    return restore_many({{this, &snapshot}});
}

Error Inventory::restore_many(const std::vector<RestoreRequest>& requests) {
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& request = requests[i];
        if (!request.target || !request.snapshot) return Error::invalid_snapshot;
        for (std::size_t j = 0; j < i; ++j) {
            if (requests[j].target == request.target) return Error::same_container;
        }
        const auto result = request.target->validate_snapshot(*request.snapshot);
        if (result != Error::none) return result;
    }
    std::vector<Snapshot> candidates;
    candidates.reserve(requests.size());
    for (const auto& request : requests) candidates.push_back(*request.snapshot);
    // All potentially throwing allocations have finished. No observers during commit.
    for (std::size_t i = 0; i < requests.size(); ++i) {
        requests[i].target->slots_.swap(candidates[i]);
    }
    return Error::none;
}

} // namespace designer::inventory
