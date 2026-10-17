#include "commerce_test_support.hpp"
#include <random>

void reseal(std::string& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < bytes.size() - 8; ++i) { hash ^= static_cast<unsigned char>(bytes[i]); hash *= 1099511628211ULL; }
    for (unsigned i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = static_cast<char>((hash >> (i * 8)) & 255);
}
int main() {
    // Independent LE fixture with one economic settlement and a linked two-bundle purchase.
    const std::string golden("\x44\x53\x48\x50\x41\x52\x43\x48\x01\x00\x00\x00\x89\x00\x00\x00\x44\x45\x43\x4f\x41\x52\x43\x48\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x2c\x01\x00\x00\x00\x00\x00\x00\x29\x00\x00\x00\x44\x49\x4e\x56\x41\x52\x43\x48\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x01\x0a\x00\x00\x00\x0a\x00\x00\x00\x01\x1b\xa1\x6a\x81\x6f\x36\xa0\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x2a\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x0a\x00\x00\x00\x0a\x00\x00\x00\xa4\x46\x9b\xde\x69\xc3\x4c\xb9\x01\x00\x00\x00\x2a\x00\x00\x00\x00\x00\x00\x00\x64\x00\x00\x00\x02\x00\x00\x00\x01\x00\x00\x00\x07\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\xe9\x19\x99\x39\xc1\xa7\x80\x9e", 197);
    commerce::Snapshot fixture;
    CHECK(commerce::decode_archive(golden, fixture) == commerce::ArchiveError::none);
    CHECK(fixture.purchases.size() == 1 && fixture.purchases[0].request.quantity == 2);
    CHECK(fixture.purchases[0].product_version == 7 && fixture.economy.balances.at(1) == 300);
    std::string golden_encoded;
    CHECK(commerce::encode_archive(fixture, golden_encoded) == commerce::ArchiveError::none && golden_encoded == golden);

    auto shop = make_shop();
    CHECK(shop.purchase({42, 100, 2, 1}).error == commerce::Error::none);
    CHECK(shop.purchase({43, 200, 1, 1}).error == commerce::Error::none);
    const auto original = shop.snapshot();
    std::string bytes;
    CHECK(commerce::encode_archive(original, bytes) == commerce::ArchiveError::none);
    commerce::Snapshot decoded;
    CHECK(commerce::decode_archive(bytes, decoded) == commerce::ArchiveError::none && equal(original, decoded));
    const auto before = decoded;
    std::string canonical;
    CHECK(commerce::encode_archive(decoded, canonical) == commerce::ArchiveError::none && canonical == bytes);
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        CHECK(commerce::decode_archive(std::string_view(bytes).substr(0, length), decoded) != commerce::ArchiveError::none);
        CHECK(equal(before, decoded));
    }
    auto corrupt = bytes; corrupt[20] ^= 1;
    CHECK(commerce::decode_archive(corrupt, decoded) == commerce::ArchiveError::checksum_mismatch && equal(before, decoded));
    corrupt = bytes; corrupt[8] = 2;
    CHECK(commerce::decode_archive(corrupt, decoded) == commerce::ArchiveError::unsupported_version && equal(before, decoded));
    corrupt = bytes; corrupt.insert(corrupt.end() - 8, 'x'); reseal(corrupt);
    CHECK(commerce::decode_archive(corrupt, decoded) == commerce::ArchiveError::malformed && equal(before, decoded));
    corrupt = bytes; corrupt[12] = static_cast<char>(255); corrupt[13] = static_cast<char>(255);
    corrupt[14] = static_cast<char>(255); corrupt[15] = static_cast<char>(255); reseal(corrupt);
    CHECK(commerce::decode_archive(corrupt, decoded) == commerce::ArchiveError::too_large && equal(before, decoded));
    // A valid outer checksum must not conceal a mismatched economic sequence.
    corrupt = bytes; corrupt[corrupt.size() - 16] = 99; reseal(corrupt);
    CHECK(commerce::decode_archive(corrupt, decoded) == commerce::ArchiveError::malformed && equal(before, decoded));
    corrupt = std::string(commerce::archive_max_bytes + 1, 'x');
    CHECK(commerce::decode_archive(corrupt, decoded) == commerce::ArchiveError::too_large && equal(before, decoded));
    auto bad = original; bad.purchases[0].settlement.sequence = 99;
    std::string output = "keep";
    CHECK(commerce::encode_archive(bad, output) == commerce::ArchiveError::malformed && output == "keep");
    bad = original; bad.purchases[0].request.destination = 2;
    CHECK(commerce::encode_archive(bad, output) == commerce::ArchiveError::malformed && output == "keep");
    bad = original; bad.purchases[1] = bad.purchases[0];
    CHECK(commerce::encode_archive(bad, output) == commerce::ArchiveError::malformed && output == "keep");
    bad = original; bad.purchases[0].product_version = 0;
    CHECK(commerce::encode_archive(bad, output) == commerce::ArchiveError::malformed && output == "keep");
    bad = original; bad.purchases.resize(commerce::max_history + 1);
    CHECK(commerce::encode_archive(bad, output) == commerce::ArchiveError::too_large && output == "keep");
    auto restored = make_shop(); restored.replace_catalog(commerce::ProductCatalog({}));
    CHECK(restored.restore(decoded) == commerce::Error::none);
    CHECK(restored.purchase({42, 100, 2, 1}).replayed && equal(original, restored.snapshot()));
    std::mt19937 random(1729);
    for (int i = 0; i < 2000; ++i) {
        auto mutated = bytes;
        mutated[random() % mutated.size()] ^= static_cast<char>(1 + random() % 255);
        if (i % 2 == 0) reseal(mutated);
        const auto saved = decoded;
        const auto result = commerce::decode_archive(mutated, decoded);
        if (result != commerce::ArchiveError::none) CHECK(equal(saved, decoded));
        else {
            CHECK(commerce::encode_archive(decoded, canonical) == commerce::ArchiveError::none);
            CHECK(canonical == mutated);
        }
    }
    std::cout << "Shop format, links, corruption handling and 2000 parser mutations passed\n";
}
