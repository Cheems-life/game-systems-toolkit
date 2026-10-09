#include <designer/inventory/archive.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>

namespace inv = designer::inventory;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Check failed at " << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (false)
std::uint64_t total(const inv::Inventory& bag, inv::ItemId id) {
    std::uint64_t result = 0;
    for (const auto& slot : bag.slots()) if (slot && slot->item_id == id) result += slot->quantity;
    return result;
}
void failure(inv::Inventory& source, inv::Inventory& target, inv::ItemId item,
             inv::Quantity amount, inv::Error expected) {
    const auto before_source = source.snapshot(), before_target = target.snapshot();
    CHECK(source.transfer_to(target, item, amount) == expected);
    CHECK(source.slots() == before_source && target.slots() == before_target);
}
int main() {
    inv::ItemCatalog rules({{1, 10}, {2, 1}});
    inv::Inventory chest(4, rules), bag(3, rules), warehouse(5, rules);
    CHECK(chest.add(1, 25) == inv::Error::none);
    CHECK(chest.add(2, 1) == inv::Error::none);
    auto chest_before = chest.snapshot(), bag_before = bag.snapshot();
    CHECK(chest.transfer_all_to(bag) == inv::Error::insufficient_capacity);
    CHECK(chest.slots() == chest_before && bag.slots() == bag_before);
    CHECK(chest.transfer_to(bag, 1, 15) == inv::Error::none);
    CHECK(total(chest, 1) == 10 && total(bag, 1) == 15);
    CHECK(bag.transfer_to(warehouse, 1, 10) == inv::Error::none);
    CHECK(warehouse.transfer_to(bag, 1, 4) == inv::Error::none);
    CHECK(total(bag, 1) == 9 && total(warehouse, 1) == 6);
    CHECK(chest.transfer_all_to(bag) == inv::Error::none);
    CHECK(total(chest, 1) == 0 && total(chest, 2) == 0);
    CHECK(total(bag, 1) + total(warehouse, 1) == 25);
    CHECK(total(bag, 2) == 1);
    failure(bag, bag, 1, 1, inv::Error::same_container);
    CHECK(bag.transfer_all_to(bag) == inv::Error::same_container);
    failure(chest, bag, 1, 1, inv::Error::insufficient_quantity);
    failure(bag, warehouse, 1, 0, inv::Error::invalid_quantity);
    failure(bag, warehouse, 99, 1, inv::Error::unknown_item);
    inv::Inventory alien(3, inv::ItemCatalog({{2, 1}}));
    failure(bag, alien, 1, 1, inv::Error::unknown_item);
    const auto alien_before = alien.snapshot();
    bag_before = bag.snapshot();
    CHECK(bag.transfer_all_to(alien) == inv::Error::unknown_item);
    CHECK(bag.slots() == bag_before && alien.slots() == alien_before);
    inv::Inventory smaller_stacks(5, inv::ItemCatalog({{1, 2}, {2, 1}}));
    CHECK(warehouse.transfer_all_to(smaller_stacks) == inv::Error::none);
    CHECK(smaller_stacks.slots()[0]->quantity == 2 && smaller_stacks.slots()[2]->quantity == 2);
    CHECK(warehouse.transfer_all_to(smaller_stacks) == inv::Error::none); // Empty source is valid.

    const auto original = bag.snapshot();
    auto invalid = original;
    invalid.push_back(std::nullopt);
    CHECK(bag.restore(invalid) == inv::Error::slot_count_mismatch);
    CHECK(bag.slots() == original);
    invalid = original;
    invalid[0] = inv::ItemStack{99, 1};
    CHECK(bag.restore(invalid) == inv::Error::unknown_item);
    CHECK(bag.slots() == original);
    invalid[0] = inv::ItemStack{1, 0};
    CHECK(bag.restore(invalid) == inv::Error::invalid_stack);
    CHECK(bag.slots() == original);
    invalid[0] = inv::ItemStack{1, 11};
    CHECK(bag.restore(invalid) == inv::Error::invalid_stack);
    CHECK(bag.slots() == original);
    inv::Snapshot empty_bag(3), invalid_warehouse(6);
    const auto warehouse_before = warehouse.snapshot();
    CHECK(inv::Inventory::restore_many({{&bag, &empty_bag}, {&warehouse, &invalid_warehouse}}) == inv::Error::slot_count_mismatch);
    CHECK(bag.slots() == original && warehouse.slots() == warehouse_before);
    CHECK(inv::Inventory::restore_many({{nullptr, &empty_bag}}) == inv::Error::invalid_snapshot);
    CHECK(inv::Inventory::restore_many({{&bag, nullptr}}) == inv::Error::invalid_snapshot);
    CHECK(inv::Inventory::restore_many({{&bag, &empty_bag}, {&bag, &empty_bag}}) == inv::Error::same_container);
    CHECK(bag.restore(bag.slots()) == inv::Error::none); // Aliased read view is staged safely.
    CHECK(bag.slots() == original);
    inv::Inventory alias_first(3, rules), alias_second(3, rules);
    CHECK(alias_first.add(1, 2) == inv::Error::none);
    CHECK(alias_second.add(2, 1) == inv::Error::none);
    const auto first_snapshot = alias_first.snapshot(), second_snapshot = alias_second.snapshot();
    CHECK(inv::Inventory::restore_many({{&alias_first, &alias_second.slots()},
                                       {&alias_second, &alias_first.slots()}}) == inv::Error::none);
    CHECK(alias_first.slots() == second_snapshot && alias_second.slots() == first_snapshot);

    inv::Inventory left(8, rules), right(8, rules);
    CHECK(left.add(1, 50) == inv::Error::none);
    CHECK(right.add(2, 4) == inv::Error::none);
    std::mt19937 random(20261009);
    for (int i = 0; i < 5000; ++i) {
        auto& source = random() % 2 ? left : right;
        auto& target = &source == &left ? right : left;
        const auto first = left.snapshot(), second = right.snapshot();
        const auto result = random() % 5 == 0 ? source.transfer_all_to(target)
            : source.transfer_to(target, 1 + random() % 2, 1 + random() % 30);
        if (result != inv::Error::none) CHECK(left.slots() == first && right.slots() == second);
        CHECK(total(left, 1) + total(right, 1) == 50);
        CHECK(total(left, 2) + total(right, 2) == 4);
        for (const auto* container : {&left, &right}) for (const auto& slot : container->slots()) if (slot)
            CHECK(slot->quantity > 0 && slot->quantity <= rules.find(slot->item_id)->max_stack);
    }
    const auto max = std::numeric_limits<inv::Quantity>::max();
    inv::Inventory huge_source(2, inv::ItemCatalog({{1, max}})), huge_target(2, inv::ItemCatalog({{1, max}}));
    CHECK(huge_source.add(1, max) == inv::Error::none);
    CHECK(huge_source.add(1, max) == inv::Error::none);
    CHECK(huge_source.transfer_all_to(huge_target) == inv::Error::none);
    CHECK(total(huge_target, 1) == std::uint64_t(max) * 2);
    std::cout << "Restore invariants and 5000 cross-container operations passed\n";
}
