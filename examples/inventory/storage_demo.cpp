#include <designer/inventory/archive.hpp>
#include <designer/persistence/file_store.hpp>
#include <iostream>

namespace inv = designer::inventory;
namespace store = designer::persistence;
void print(const char* name, const inv::Inventory& bag) {
    std::cout << name << ":";
    for (const auto& slot : bag.slots()) {
        if (slot) std::cout << " [" << slot->item_id << " x " << slot->quantity << "]";
        else std::cout << " [empty]";
    }
    std::cout << '\n';
}
bool operation(const char* name, inv::Error result) {
    std::cout << name << ": " << inv::error_name(result) << '\n';
    return result == inv::Error::none;
}
int main(int argc, char** argv) {
    const bool load_only = argc == 3 && std::string_view(argv[2]) == "--load";
    if (argc != 2 && !load_only) {
        std::cerr << "Usage: designer_inventory_storage_demo <save-path> [--load]\n"
                  << "Without --load, this demonstration replaces the file. Use a dedicated example path.\n";
        return 1;
    }
    inv::ItemCatalog rules({{1001, 20}, {2001, 1}});
    inv::Inventory chest(3, rules), bag(3, rules), warehouse(6, rules);
    if (!load_only) {
        if (!operation("Fill chest with potions", chest.add(1001, 25)) ||
            !operation("Fill chest with sword", chest.add(2001, 1)) ||
            !operation("Loot entire chest", chest.transfer_all_to(bag)) ||
            !operation("Deposit 15 potions", bag.transfer_to(warehouse, 1001, 15)) ||
            !operation("Withdraw 4 potions", warehouse.transfer_to(bag, 1001, 4))) return 1;
        print("Chest", chest); print("Bag", bag); print("Warehouse", warehouse);
        inv::Archive archive{{1, bag.snapshot()}, {2, warehouse.snapshot()}, {3, chest.snapshot()}};
        std::string bytes;
        auto result = inv::encode_archive(archive, bytes);
        if (result.error != inv::ArchiveError::none) {
            std::cerr << inv::archive_error_name(result.error) << '\n'; return 1;
        }
        const auto saved = store::save_atomic(argv[1], bytes);
        if (saved.error != store::FileError::none) {
            std::cerr << store::file_error_name(saved.error) << "; committed=" << saved.committed << '\n';
            return 1;
        }
    }
    std::string bytes;
    inv::ArchiveResult result;
    inv::Inventory restored_bag(3, rules), restored_warehouse(6, rules), restored_chest(3, rules);
    const auto loaded = store::load_file(argv[1], bytes);
    if (loaded != store::FileError::none) { std::cerr << store::file_error_name(loaded) << '\n'; return 1; }
    inv::Archive decoded;
    result = inv::decode_archive(bytes, decoded);
    if (result.error == inv::ArchiveError::none)
        result = inv::restore_archive(decoded, {{1, &restored_bag}, {2, &restored_warehouse}, {3, &restored_chest}});
    if (result.error != inv::ArchiveError::none) {
        std::cerr << inv::archive_error_name(result.error) << ": " << inv::error_name(result.inventory_error) << '\n';
        return 1;
    }
    if (!load_only && (restored_bag.slots() != bag.slots() || restored_warehouse.slots() != warehouse.slots() ||
        restored_chest.slots() != chest.slots())) return 1;
    std::cout << "Restored all containers together" << (load_only ? " from previous run.\n" : " after saving.\n");
    print("Restored chest", restored_chest);
    print("Restored bag", restored_bag); print("Restored warehouse", restored_warehouse);
}
