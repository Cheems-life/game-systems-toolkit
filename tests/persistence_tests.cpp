#include <designer/persistence/file_store.hpp>
#include <designer/inventory/archive.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>

namespace fs = std::filesystem;
namespace store = designer::persistence;
namespace inv = designer::inventory;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Check failed at " << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (false)
struct TemporaryDirectory {
    fs::path path;
    explicit TemporaryDirectory(const fs::path& parent) {
        std::random_device random;
        for (int i = 0; i < 100; ++i) {
            auto candidate = parent / ("persistence-test-" + std::to_string(random()));
            if (fs::create_directory(candidate)) { path = std::move(candidate); return; }
        }
        throw std::runtime_error("Cannot create isolated test directory");
    }
    ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};
int main(int argc, char** argv) {
    CHECK(argc == 2);
    TemporaryDirectory temporary(argv[1]);
    const auto file = temporary.path / "player.sav";
    std::string loaded = "unchanged";
    CHECK(store::load_file(file, loaded) == store::FileError::not_found);
    CHECK(loaded == "unchanged");
    CHECK(store::load_file({}, loaded) == store::FileError::invalid_path);
    CHECK(store::save_atomic({}, "data").error == store::FileError::invalid_path);
    const auto saved = store::save_atomic(file, std::string("old\0data", 8));
#if defined(__unix__) || defined(__APPLE__)
    CHECK(saved.error == store::FileError::none && saved.committed);
    CHECK(store::load_file(file, loaded) == store::FileError::none && loaded == std::string("old\0data", 8));
    auto failed = store::save_atomic(file, "too large", 1);
    CHECK(failed.error == store::FileError::too_large && !failed.committed);
    CHECK(store::load_file(file, loaded) == store::FileError::none && loaded == std::string("old\0data", 8));
    loaded = "keep";
    CHECK(store::load_file(file, loaded, 1) == store::FileError::too_large && loaded == "keep");
    failed = store::save_atomic(temporary.path / "missing" / "file", "data");
    CHECK(failed.error == store::FileError::io_error && !failed.committed);
    const auto directory_destination = temporary.path / "directory";
    CHECK(fs::create_directory(directory_destination));
    failed = store::save_atomic(directory_destination, "data"); // Force failure AFTER writing temporary file.
    CHECK(failed.error == store::FileError::io_error && !failed.committed);
    CHECK(fs::is_directory(directory_destination));
    for (const auto& entry : fs::directory_iterator(temporary.path))
        CHECK(entry.path().filename().string().find(".tmp.") == std::string::npos);
    CHECK(store::save_atomic(file, "new").error == store::FileError::none);
    CHECK(store::load_file(file, loaded) == store::FileError::none && loaded == "new");
    CHECK(store::save_atomic(file, "").error == store::FileError::none);
    CHECK(store::load_file(file, loaded) == store::FileError::none && loaded.empty());

    // Real file round-trip of related containers after a warehouse transaction.
    inv::ItemCatalog rules({{1, 10}, {2, 1}});
    inv::Inventory chest(2, rules), bag(3, rules), warehouse(4, rules);
    CHECK(chest.add(1, 12) == inv::Error::none);
    CHECK(chest.transfer_all_to(bag) == inv::Error::none);
    CHECK(bag.transfer_to(warehouse, 1, 7) == inv::Error::none);
    inv::Archive snapshot{{1, bag.snapshot()}, {2, warehouse.snapshot()}, {3, chest.snapshot()}};
    std::string bytes;
    CHECK(inv::encode_archive(snapshot, bytes).error == inv::ArchiveError::none);
    CHECK(store::save_atomic(file, bytes).error == store::FileError::none);
    CHECK(store::load_file(file, loaded) == store::FileError::none);
    inv::Archive restored;
    CHECK(inv::decode_archive(loaded, restored).error == inv::ArchiveError::none);
    inv::Inventory new_bag(3, rules), new_warehouse(4, rules), new_chest(2, rules);
    CHECK(inv::restore_archive(restored, {{3, &new_chest}, {1, &new_bag}, {2, &new_warehouse}}).error == inv::ArchiveError::none);
    CHECK(new_bag.slots() == bag.slots() && new_warehouse.slots() == warehouse.slots() && new_chest.slots() == chest.slots());
    loaded[25] ^= 1;
    const auto preserved = restored;
    CHECK(inv::decode_archive(loaded, restored).error == inv::ArchiveError::checksum_mismatch);
    CHECK(restored.size() == preserved.size() && restored[0].slots == preserved[0].slots);
#else
    CHECK(saved.error == store::FileError::unsupported_platform && !saved.committed);
#endif
    std::cout << "Bounded I/O, atomic replacement and complete container round-trip passed\n";
}
