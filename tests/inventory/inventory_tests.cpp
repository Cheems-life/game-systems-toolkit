// 背包基础规则测试：检查添加、移除、拆堆、合并、交换及非法参数、容量和整数边界。
// 用固定种子的 10000 次连续操作，对照独立数量模型检查数量守恒和失败时状态不变。

#include <designer/inventory/inventory.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace inv = designer::inventory;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Check failed at line " << __LINE__ << ": " #condition "\n"; \
    std::exit(1); } } while (false)

std::uint64_t total(const inv::Inventory& bag, inv::ItemId id) {
    std::uint64_t count = 0;
    for (const auto& slot : bag.slots()) if (slot && slot->item_id == id) count += slot->quantity;
    return count;
}

void expect_failure(inv::Inventory& bag, inv::Error expected, inv::ItemId id, inv::Quantity amount, bool add) {
    const auto before = bag.slots();
    CHECK((add ? bag.add(id, amount) : bag.remove(id, amount)) == expected);
    CHECK(bag.slots() == before);
}

int main() {
    for (const auto& definitions : {std::vector<inv::ItemDefinition>{{0, 1}},
                                   std::vector<inv::ItemDefinition>{{1, 0}},
                                   std::vector<inv::ItemDefinition>{{1, 2}, {1, 3}}}) {
        bool rejected = false;
        try { inv::ItemCatalog catalog(definitions); }
        catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
    inv::ItemCatalog catalog({{1, 10}, {2, 1}});
    inv::Inventory bag(3, catalog);
    expect_failure(bag, inv::Error::invalid_quantity, 1, 0, true);
    expect_failure(bag, inv::Error::unknown_item, 99, 1, true);
    expect_failure(bag, inv::Error::unknown_item, 99, 1, false);
    expect_failure(bag, inv::Error::invalid_quantity, 1, 0, false);
    CHECK(bag.add(1, 25) == inv::Error::none);
    CHECK(bag.slots()[0]->quantity == 10 && bag.slots()[2]->quantity == 5);
    expect_failure(bag, inv::Error::insufficient_capacity, 1, 6, true);
    expect_failure(bag, inv::Error::insufficient_quantity, 1, 26, false);
    CHECK(bag.add(1, 5) == inv::Error::none); // Full bag with room in existing stack.
    CHECK(bag.remove(1, 12) == inv::Error::none);
    CHECK(!bag.slots()[0] && bag.slots()[1]->quantity == 8);
    CHECK(bag.add(1, 1) == inv::Error::none);
    CHECK(!bag.slots()[0] && bag.slots()[1]->quantity == 9); // Existing stacks first.
    CHECK(bag.move(1, 0, 3) == inv::Error::none);
    CHECK(bag.slots()[0]->quantity == 3 && bag.slots()[1]->quantity == 6);
    CHECK(bag.move(0, 1, 3) == inv::Error::none);
    CHECK(!bag.slots()[0] && bag.slots()[1]->quantity == 9);
    CHECK(bag.add(2, 1) == inv::Error::none);
    auto before = bag.slots();
    CHECK(bag.move(1, 0, 1) == inv::Error::incompatible_item);
    CHECK(bag.slots() == before);
    CHECK(bag.move(1, 2, 1) == inv::Error::insufficient_capacity);
    CHECK(bag.slots() == before);
    CHECK(bag.move(1, 0, 10) == inv::Error::insufficient_quantity);
    CHECK(bag.slots() == before);
    CHECK(bag.move(3, 0, 1) == inv::Error::invalid_slot);
    CHECK(bag.move(0, 3, 1) == inv::Error::invalid_slot);
    CHECK(bag.move(0, 1, 0) == inv::Error::invalid_quantity);
    CHECK(bag.swap_slots(0, 3) == inv::Error::invalid_slot);
    CHECK(bag.slots() == before);
    CHECK(bag.move(1, 1, 9) == inv::Error::none);
    CHECK(bag.move(1, 1, 10) == inv::Error::insufficient_quantity);
    CHECK(bag.slots() == before);
    CHECK(bag.swap_slots(0, 2) == inv::Error::none);
    CHECK(bag.slots()[2]->item_id == 2);
    CHECK(bag.remove(1, 19) == inv::Error::none);
    CHECK(!bag.slots()[0] && !bag.slots()[1]);
    CHECK(bag.move(0, 1, 1) == inv::Error::empty_source);
    CHECK(bag.swap_slots(1, 2) == inv::Error::none);
    CHECK(!bag.slots()[2] && bag.slots()[1]->item_id == 2);
    before = bag.slots();
    CHECK(bag.swap_slots(1, 1) == inv::Error::none);
    CHECK(bag.swap_slots(0, 2) == inv::Error::none);
    CHECK(bag.slots() == before);

    inv::Inventory empty(0, catalog);
    expect_failure(empty, inv::Error::insufficient_capacity, 1, 1, true);
    expect_failure(empty, inv::Error::insufficient_quantity, 1, 1, false);
    CHECK(empty.swap_slots(0, 0) == inv::Error::invalid_slot);
    inv::Inventory no_rules(1, inv::ItemCatalog({}));
    expect_failure(no_rules, inv::Error::unknown_item, 1, 1, true);

    const auto max = std::numeric_limits<inv::Quantity>::max();
    inv::Inventory large(2, inv::ItemCatalog({{1, max}}));
    CHECK(large.add(1, max) == inv::Error::none);
    CHECK(large.add(1, max) == inv::Error::none);
    CHECK(total(large, 1) == static_cast<std::uint64_t>(max) * 2);
    before = large.slots();
    CHECK(large.move(0, 1, 1) == inv::Error::insufficient_capacity);
    CHECK(large.slots() == before);
    expect_failure(large, inv::Error::insufficient_capacity, 1, 1, true);
    CHECK(large.remove(1, max) == inv::Error::none);
    CHECK(large.move(1, 0, max) == inv::Error::none);
    CHECK(large.slots()[0]->quantity == max && !large.slots()[1]);

    // Reproducible stateful sequence: check totals independently from slot layout.
    inv::Inventory random_bag(8, catalog);
    std::mt19937 random(20261009);
    std::uint64_t expected[2] = {0, 0};
    for (int step = 0; step < 10000; ++step) {
        const inv::ItemId id = 1 + random() % 2;
        const inv::Quantity amount = 1 + random() % 15;
        const auto operation = random() % 4;
        const std::size_t source = random() % 10;
        const std::size_t destination = random() % 10;
        before = random_bag.slots();
        inv::Error result;
        if (operation == 0) {
            std::uint64_t room = 0;
            for (const auto& slot : before) {
                if (!slot) room += catalog.find(id)->max_stack;
                else if (slot->item_id == id) room += catalog.find(id)->max_stack - slot->quantity;
            }
            result = random_bag.add(id, amount);
            CHECK(result == (room >= amount ? inv::Error::none : inv::Error::insufficient_capacity));
            if (result == inv::Error::none) expected[id - 1] += amount;
        } else if (operation == 1) {
            result = random_bag.remove(id, amount);
            CHECK(result == (expected[id - 1] >= amount ? inv::Error::none : inv::Error::insufficient_quantity));
            if (result == inv::Error::none) expected[id - 1] -= amount;
        } else if (operation == 2) {
            result = random_bag.move(source, destination, amount);
        } else {
            result = random_bag.swap_slots(source, destination);
        }
        if (result != inv::Error::none) CHECK(random_bag.slots() == before);
        CHECK(random_bag.slots().size() == 8);
        for (const auto& slot : random_bag.slots()) if (slot) {
            const auto* definition = catalog.find(slot->item_id);
            CHECK(definition && slot->quantity > 0 && slot->quantity <= definition->max_stack);
        }
        CHECK(total(random_bag, 1) == expected[0]);
        CHECK(total(random_bag, 2) == expected[1]);
    }
    std::cout << "Inventory boundaries and 10000 stateful operations passed\n";
}
