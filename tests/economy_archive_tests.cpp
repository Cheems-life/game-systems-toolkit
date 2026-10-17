// 经济存档格式测试：检查余额、容器和成功交易记录的统一编码/解码及恢复后去重。
// 覆盖固定格式样本、截断、超限、非法历史与 2000 次字节变异，检查失败不修改输出。

#include "economy_test_support.hpp"
#include <random>

void reseal(std::string& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < bytes.size() - 8; ++i) { hash ^= static_cast<unsigned char>(bytes[i]); hash *= 1099511628211ULL; }
    for (unsigned i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = static_cast<char>((hash >> (i * 8)) & 255);
}
int main() {
    // Independently constructed v1 fixture includes a balance requiring all eight bytes.
    const std::string golden("\x44\x45\x43\x4f\x41\x52\x43\x48\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x08\x07\x06\x05\x04\x03\x02\x01\x18\x00\x00\x00\x44\x49\x4e\x56\x41\x52\x43\x48\x01\x00\x00\x00\x00\x00\x00\x00\xfb\xcc\x5e\x59\x98\x91\x5b\x67\x00\x00\x00\x00\x44\x17\xd6\x98\x54\xd2\xbd\xc8", 68);
    eco::Snapshot golden_snapshot;
    CHECK(eco::decode_archive(golden, golden_snapshot) == eco::ArchiveError::none);
    CHECK(golden_snapshot.balances.at(1) == 0x0102030405060708ULL);
    CHECK(golden_snapshot.containers.empty() && golden_snapshot.receipts.empty());
    std::string golden_encoded;
    CHECK(eco::encode_archive(golden_snapshot, golden_encoded) == eco::ArchiveError::none && golden_encoded == golden);

    auto economy = make_economy();
    CHECK(economy.execute(purchase()).error == eco::Error::none);
    const auto original = economy.snapshot();
    std::string bytes;
    CHECK(eco::encode_archive(original, bytes) == eco::ArchiveError::none);
    eco::Snapshot decoded;
    CHECK(eco::decode_archive(bytes, decoded) == eco::ArchiveError::none && equal(original, decoded));
    const auto before = decoded;
    std::string encoded_again;
    CHECK(eco::encode_archive(decoded, encoded_again) == eco::ArchiveError::none && bytes == encoded_again);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        CHECK(eco::decode_archive(std::string_view(bytes).substr(0, i), decoded) != eco::ArchiveError::none);
        CHECK(equal(before, decoded));
    }
    auto invalid = bytes; invalid[20] ^= 1;
    CHECK(eco::decode_archive(invalid, decoded) == eco::ArchiveError::checksum_mismatch && equal(decoded, before));
    invalid = bytes; invalid[8] = 2;
    CHECK(eco::decode_archive(invalid, decoded) == eco::ArchiveError::unsupported_version && equal(decoded, before));
    invalid = bytes; invalid.insert(invalid.end() - 8, 'x'); reseal(invalid);
    CHECK(eco::decode_archive(invalid, decoded) == eco::ArchiveError::malformed && equal(decoded, before));
    invalid = bytes; invalid[12] = static_cast<char>(255); invalid[13] = static_cast<char>(255); reseal(invalid);
    CHECK(eco::decode_archive(invalid, decoded) == eco::ArchiveError::too_large && equal(decoded, before));
    invalid = std::string(eco::archive_max_bytes + 1, 'x');
    CHECK(eco::decode_archive(invalid, decoded) == eco::ArchiveError::too_large && equal(decoded, before));
    auto invalid_snapshot = original; invalid_snapshot.receipts[0].sequence = 2;
    std::string output = "keep";
    CHECK(eco::encode_archive(invalid_snapshot, output) == eco::ArchiveError::malformed && output == "keep");
    invalid_snapshot = original; invalid_snapshot.receipts.push_back(original.receipts[0]);
    invalid_snapshot.receipts.back().sequence = 2;
    CHECK(eco::encode_archive(invalid_snapshot, output) == eco::ArchiveError::malformed && output == "keep");
    invalid_snapshot = original; invalid_snapshot.balances[3] = 0;
    CHECK(eco::encode_archive(invalid_snapshot, output) == eco::ArchiveError::malformed && output == "keep");
    invalid_snapshot = original; invalid_snapshot.receipts[0].transaction.reward.items = {{10, 2}, {10, 3}};
    CHECK(eco::encode_archive(invalid_snapshot, output) == eco::ArchiveError::malformed && output == "keep");
    invalid_snapshot = original; invalid_snapshot.receipts.resize(eco::max_history + 1);
    CHECK(eco::encode_archive(invalid_snapshot, output) == eco::ArchiveError::too_large && output == "keep");
    auto restored = make_economy();
    CHECK(restored.restore(decoded) == eco::Error::none);
    CHECK(restored.execute(purchase()).replayed && equal(original, restored.snapshot()));

    std::mt19937 random(1314);
    for (int i = 0; i < 2000; ++i) {
        auto mutated = bytes;
        mutated[random() % mutated.size()] ^= static_cast<char>(1 + random() % 255);
        if (i % 2 == 0) reseal(mutated);
        const auto saved = decoded;
        const auto result = eco::decode_archive(mutated, decoded);
        if (result != eco::ArchiveError::none) CHECK(equal(saved, decoded));
        else {
            CHECK(eco::encode_archive(decoded, encoded_again) == eco::ArchiveError::none);
            CHECK(mutated == encoded_again);
        }
    }
    std::cout << "Economy archive round-trip, corrupt data and 2000 parser mutations passed\n";
}
