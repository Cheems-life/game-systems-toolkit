#include <designer/inventory/archive.hpp>
#include <cstdlib>
#include <iostream>
#include <random>

namespace inv = designer::inventory;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Check failed at " << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (false)
void reseal(std::string& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < bytes.size() - 8; ++i) { hash ^= static_cast<unsigned char>(bytes[i]); hash *= 1099511628211ULL; }
    for (unsigned i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = static_cast<char>((hash >> (8 * i)) & 255);
}
bool equal(const inv::Archive& a, const inv::Archive& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i].id != b[i].id || a[i].slots != b[i].slots) return false;
    return true;
}
int main() {
    // Fixed version-1 fixture, independently constructed with little-endian fields.
    const std::string golden("\x44\x49\x4e\x56\x41\x52\x43\x48\x01\x00\x00\x00\x01\x00\x00\x00\x11\x00\x00\x00\x02\x00\x00\x00\x01\xe9\x03\x00\x00\x08\x00\x00\x00\x00\x88\xeb\x1b\x9d\xb3\xce\x39\x20", 42);
    inv::Archive golden_archive;
    CHECK(inv::decode_archive(golden, golden_archive).error == inv::ArchiveError::none);
    CHECK(golden_archive.size() == 1 && golden_archive[0].id == 17);
    CHECK(golden_archive[0].slots.size() == 2 && !golden_archive[0].slots[1]);
    CHECK(golden_archive[0].slots[0]->item_id == 1001 && golden_archive[0].slots[0]->quantity == 8);
    std::string golden_output;
    CHECK(inv::encode_archive(golden_archive, golden_output).error == inv::ArchiveError::none);
    CHECK(golden_output == golden);

    inv::Archive original{{10, {inv::ItemStack{1, 8}, std::nullopt}}, {20, {inv::ItemStack{2, 1}}}};
    std::string encoded;
    CHECK(inv::encode_archive(original, encoded).error == inv::ArchiveError::none);
    inv::Archive decoded;
    CHECK(inv::decode_archive(encoded, decoded).error == inv::ArchiveError::none);
    CHECK(equal(decoded, original));
    std::string canonical;
    CHECK(inv::encode_archive(decoded, canonical).error == inv::ArchiveError::none && canonical == encoded);
    const auto preserved = decoded;
    for (std::size_t size = 0; size < encoded.size(); ++size) {
        CHECK(inv::decode_archive(std::string_view(encoded).substr(0, size), decoded).error != inv::ArchiveError::none);
        CHECK(equal(decoded, preserved));
    }
    auto corrupted = encoded;
    corrupted[25] ^= 1;
    CHECK(inv::decode_archive(corrupted, decoded).error == inv::ArchiveError::checksum_mismatch);
    CHECK(equal(decoded, preserved));
    corrupted = encoded; corrupted[8] = 2;
    CHECK(inv::decode_archive(corrupted, decoded).error == inv::ArchiveError::unsupported_version);
    CHECK(equal(decoded, preserved));
    corrupted = encoded; corrupted[24] = 2; reseal(corrupted); // Illegal slot tag, valid checksum.
    CHECK(inv::decode_archive(corrupted, decoded).error == inv::ArchiveError::malformed);
    corrupted = encoded; corrupted[24] = 1; corrupted[29] = 0; reseal(corrupted);
    CHECK(inv::decode_archive(corrupted, decoded).error == inv::ArchiveError::malformed);
    corrupted = encoded; corrupted[20] = static_cast<char>(255); corrupted[21] = static_cast<char>(255);
    corrupted[22] = static_cast<char>(255); corrupted[23] = static_cast<char>(255); reseal(corrupted);
    CHECK(inv::decode_archive(corrupted, decoded).error == inv::ArchiveError::too_large);
    corrupted = encoded; corrupted.insert(corrupted.end() - 8, 'x'); reseal(corrupted);
    CHECK(inv::decode_archive(corrupted, decoded).error == inv::ArchiveError::malformed);
    corrupted = encoded; corrupted[34] = 10; reseal(corrupted); // Second container duplicates ID 10.
    CHECK(inv::decode_archive(corrupted, decoded).error == inv::ArchiveError::duplicate_container);
    CHECK(equal(decoded, preserved));
    std::string unchanged = "keep";
    CHECK(inv::encode_archive({{1, {}}, {1, {}}}, unchanged).error == inv::ArchiveError::duplicate_container);
    CHECK(unchanged == "keep");
    CHECK(inv::encode_archive({{0, {}}}, unchanged).error == inv::ArchiveError::invalid_container);
    CHECK(inv::encode_archive({{1, {inv::ItemStack{1, 0}}}}, unchanged).error == inv::ArchiveError::malformed);
    CHECK(inv::encode_archive({{1, inv::Snapshot(inv::archive_max_slots_per_container + 1)}}, unchanged).error == inv::ArchiveError::too_large);
    CHECK(unchanged == "keep");
    inv::Archive many(inv::archive_max_containers + 1);
    CHECK(inv::encode_archive(many, unchanged).error == inv::ArchiveError::too_large);
    CHECK(inv::encode_archive({{1, inv::Snapshot(100000)}, {2, inv::Snapshot(100000)},
                               {3, inv::Snapshot(1)}}, unchanged).error == inv::ArchiveError::too_large);
    CHECK(unchanged == "keep");
    std::string oversized(inv::archive_max_bytes + 1, 'x');
    CHECK(inv::decode_archive(oversized, decoded).error == inv::ArchiveError::too_large);
    CHECK(equal(decoded, preserved));
    CHECK(inv::encode_archive({}, unchanged).error == inv::ArchiveError::none);
    CHECK(inv::decode_archive(unchanged, decoded).error == inv::ArchiveError::none && decoded.empty());

    inv::ItemCatalog rules({{1, 10}, {2, 1}});
    inv::Inventory bag(2, rules), warehouse(1, rules);
    CHECK(inv::restore_archive(original, {{20, &warehouse}, {10, &bag}}).error == inv::ArchiveError::none);
    CHECK(bag.slots() == original[0].slots && warehouse.slots() == original[1].slots);
    auto invalid = original; invalid[0].slots = inv::Snapshot(2); invalid[1].slots[0] = inv::ItemStack{2, 2};
    const auto before_bag = bag.snapshot(), before_warehouse = warehouse.snapshot();
    const auto restored = inv::restore_archive(invalid, {{10, &bag}, {20, &warehouse}});
    CHECK(restored.error == inv::ArchiveError::invalid_inventory && restored.inventory_error == inv::Error::invalid_stack);
    CHECK(bag.slots() == before_bag && warehouse.slots() == before_warehouse);
    invalid = original; invalid[1].slots[0] = inv::ItemStack{99, 1};
    CHECK(inv::restore_archive(invalid, {{10, &bag}, {20, &warehouse}}).inventory_error == inv::Error::unknown_item);
    CHECK(inv::restore_archive(original, {{10, &bag}}).error == inv::ArchiveError::binding_mismatch);
    CHECK(inv::restore_archive(original, {{10, &bag}, {10, &warehouse}}).error == inv::ArchiveError::binding_mismatch);
    CHECK(inv::restore_archive(original, {{10, &bag}, {20, &bag}}).error == inv::ArchiveError::binding_mismatch);
    CHECK(inv::restore_archive(original, {{10, &bag}, {30, &warehouse}}).error == inv::ArchiveError::binding_mismatch);
    CHECK(inv::restore_archive(original, {{10, &bag}, {20, nullptr}}).error == inv::ArchiveError::binding_mismatch);
    CHECK(bag.slots() == before_bag && warehouse.slots() == before_warehouse);

    // Parser resilience for untrusted byte sequences, with strong output guarantee.
    std::mt19937 random(314159);
    for (int i = 0; i < 3000; ++i) {
        auto bytes = encoded;
        const auto index = random() % bytes.size();
        bytes[index] ^= static_cast<char>(1 + random() % 255);
        if (i % 2 == 0) reseal(bytes); // Also exercise structurally invalid but checksummed input.
        const auto before = decoded;
        const auto result = inv::decode_archive(bytes, decoded);
        if (result.error != inv::ArchiveError::none) CHECK(equal(before, decoded));
        else {
            CHECK(inv::encode_archive(decoded, canonical).error == inv::ArchiveError::none);
            CHECK(canonical == bytes);
        }
    }
    std::cout << "Archive format, atomic restoration and parser mutations passed\n";
}
