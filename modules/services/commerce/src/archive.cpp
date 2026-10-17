#include <designer/commerce/archive.hpp>
#include <designer/economy/archive.hpp>
#include "snapshot_validation.hpp"

#include <type_traits>

namespace designer::commerce {
static_assert(std::is_nothrow_move_assignable<Snapshot>::value, "Decoded state commit must not throw");
namespace {
constexpr std::string_view magic = "DSHPARCH";
std::uint64_t checksum(std::string_view bytes) {
    std::uint64_t result = 14695981039346656037ULL;
    for (const unsigned char byte : bytes) { result ^= byte; result *= 1099511628211ULL; }
    return result;
}
void append(std::string& bytes, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<char>((value >> (i * 8)) & 255));
}
class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}
    bool read(std::uint64_t& value, unsigned count) {
        if (count > bytes_.size() - offset_) return false;
        value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(static_cast<unsigned char>(bytes_[offset_++])) << (i * 8);
        return true;
    }
    bool slice(std::size_t length, std::string_view& output) {
        if (length > bytes_.size() - offset_) return false;
        output = bytes_.substr(offset_, length);
        offset_ += length;
        return true;
    }
    bool finished() const noexcept { return offset_ == bytes_.size(); }
private:
    std::string_view bytes_;
    std::size_t offset_ = 0;
};
} // namespace
const char* archive_error_name(ArchiveError error) noexcept {
    switch (error) {
    case ArchiveError::none: return "none";
    case ArchiveError::too_large: return "too_large";
    case ArchiveError::malformed: return "malformed";
    case ArchiveError::unsupported_version: return "unsupported_version";
    case ArchiveError::checksum_mismatch: return "checksum_mismatch";
    }
    return "unrecognized_error";
}
ArchiveError encode_archive(const Snapshot& snapshot, std::string& output) {
    if (snapshot.purchases.size() > max_history) return ArchiveError::too_large;
    if (!detail::valid_links(snapshot)) return ArchiveError::malformed;
    std::string economic_bytes;
    const auto nested = economy::encode_archive(snapshot.economy, economic_bytes);
    if (nested == economy::ArchiveError::too_large) return ArchiveError::too_large;
    if (nested != economy::ArchiveError::none) return ArchiveError::malformed;
    std::string candidate(magic);
    append(candidate, 1, 4);
    append(candidate, economic_bytes.size(), 4);
    candidate += economic_bytes;
    append(candidate, snapshot.purchases.size(), 4);
    for (const auto& purchase : snapshot.purchases) {
        append(candidate, purchase.request.request_id, 8);
        append(candidate, purchase.request.product_id, 4);
        append(candidate, purchase.request.quantity, 4);
        append(candidate, purchase.request.destination, 4);
        append(candidate, purchase.product_version, 4);
        append(candidate, purchase.settlement.sequence, 8);
    }
    append(candidate, checksum(candidate), 8);
    if (candidate.size() > archive_max_bytes) return ArchiveError::too_large;
    output.swap(candidate);
    return ArchiveError::none;
}
ArchiveError decode_archive(std::string_view bytes, Snapshot& output) {
    if (bytes.size() > archive_max_bytes) return ArchiveError::too_large;
    if (bytes.size() < 28 || bytes.substr(0, 8) != magic) return ArchiveError::malformed;
    Reader header(bytes.substr(8, 8));
    std::uint64_t version = 0, length = 0;
    if (!header.read(version, 4) || !header.read(length, 4)) return ArchiveError::malformed;
    if (version != 1) return ArchiveError::unsupported_version;
    Reader footer(bytes.substr(bytes.size() - 8));
    std::uint64_t stored_checksum = 0;
    if (!footer.read(stored_checksum, 8)) return ArchiveError::malformed;
    if (stored_checksum != checksum(bytes.substr(0, bytes.size() - 8))) return ArchiveError::checksum_mismatch;
    if (length > economy::archive_max_bytes) return ArchiveError::too_large;
    Reader reader(bytes.substr(16, bytes.size() - 24));
    std::string_view economic_bytes;
    if (!reader.slice(static_cast<std::size_t>(length), economic_bytes)) return ArchiveError::malformed;
    Snapshot candidate;
    const auto nested = economy::decode_archive(economic_bytes, candidate.economy);
    if (nested == economy::ArchiveError::too_large) return ArchiveError::too_large;
    if (nested != economy::ArchiveError::none) return ArchiveError::malformed;
    std::uint64_t count = 0;
    if (!reader.read(count, 4)) return ArchiveError::malformed;
    if (count > max_history) return ArchiveError::too_large;
    std::map<economy::RequestId, const economy::Receipt*> economic_receipts;
    for (const auto& receipt : candidate.economy.receipts) economic_receipts.emplace(receipt.transaction.request_id, &receipt);
    candidate.purchases.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t request = 0, product = 0, quantity = 0, destination = 0, product_version = 0, sequence = 0;
        if (!reader.read(request, 8) || !reader.read(product, 4) || !reader.read(quantity, 4) ||
            !reader.read(destination, 4) || !reader.read(product_version, 4) || !reader.read(sequence, 8))
            return ArchiveError::malformed;
        const auto receipt = economic_receipts.find(request);
        if (receipt == economic_receipts.end() || receipt->second->sequence != sequence) return ArchiveError::malformed;
        candidate.purchases.push_back({{request, static_cast<ProductId>(product), static_cast<Quantity>(quantity),
            static_cast<economy::ContainerId>(destination)}, static_cast<ProductVersion>(product_version), *receipt->second});
    }
    if (!reader.finished() || !detail::valid_links(candidate)) return ArchiveError::malformed;
    output = std::move(candidate);
    return ArchiveError::none;
}
} // namespace designer::commerce
