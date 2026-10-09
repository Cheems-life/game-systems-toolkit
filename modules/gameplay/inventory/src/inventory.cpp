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
    }
    return "unrecognized_error";
}

Inventory::Inventory(std::size_t slot_count, ItemCatalog catalog)
    : catalog_(std::move(catalog)), slots_(slot_count) {}

Error Inventory::add(ItemId item, Quantity quantity) {
    if (quantity == 0) return Error::invalid_quantity;
    const auto* definition = catalog_.find(item);
    if (!definition) return Error::unknown_item;

    // Subtract from remaining rather than summing capacities (which may overflow).
    auto remaining = quantity;
    for (const auto& slot : slots_) {
        const Quantity room = !slot ? definition->max_stack
            : slot->item_id == item ? definition->max_stack - slot->quantity : 0;
        remaining -= std::min(remaining, room);
        if (remaining == 0) break;
    }
    if (remaining != 0) return Error::insufficient_capacity;

    remaining = quantity;
    for (auto& slot : slots_) {
        if (slot && slot->item_id == item) {
            const auto amount = std::min(remaining, definition->max_stack - slot->quantity);
            slot->quantity += amount;
            remaining -= amount;
        }
        if (remaining == 0) return Error::none;
    }
    for (auto& slot : slots_) {
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
    if (quantity == 0) return Error::invalid_quantity;
    if (!catalog_.find(item)) return Error::unknown_item;
    auto remaining = quantity;
    for (const auto& slot : slots_) {
        if (slot && slot->item_id == item) remaining -= std::min(remaining, slot->quantity);
        if (remaining == 0) break;
    }
    if (remaining != 0) return Error::insufficient_quantity;

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

} // namespace designer::inventory
