#include <designer/inventory/inventory.hpp>
#include <iostream>

namespace inv = designer::inventory;

void print(const inv::Inventory& bag) {
    for (std::size_t i = 0; i < bag.slots().size(); ++i) {
        const auto& slot = bag.slots()[i];
        std::cout << "Slot " << i << ": ";
        if (slot) std::cout << slot->item_id << " x " << slot->quantity;
        else std::cout << "empty";
        std::cout << '\n';
    }
}

int main() {
    inv::Inventory bag(3, inv::ItemCatalog({{1001, 20}, {2001, 1}}));
    const auto run = [](const char* action, inv::Error result) {
        std::cout << action << ": " << inv::error_name(result) << '\n';
        return result == inv::Error::none;
    };
    if (!run("Add 25 potions", bag.add(1001, 25))) return 1;
    if (!run("Split 3 potions from slot 0 into slot 2", bag.move(0, 2, 3))) return 1;
    print(bag);
    const auto before = bag.slots();
    const auto failed = bag.add(2001, 1);
    run("Add sword to full bag", failed);
    if (failed != inv::Error::insufficient_capacity || bag.slots() != before) return 1;
    if (!run("Remove 5 potions", bag.remove(1001, 5))) return 1;
    if (!run("Swap slots 0 and 2", bag.swap_slots(0, 2))) return 1;
    print(bag);
}
