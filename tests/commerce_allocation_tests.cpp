#include "commerce_test_support.hpp"
#include <new>

namespace { long remaining = -1; }
void* operator new(std::size_t size) {
    if (remaining == 0) throw std::bad_alloc();
    if (remaining > 0) --remaining;
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
    bool success = false;
    auto economic_source = make_economy(1);
    CHECK(economic_source.execute(purchase()).error == eco::Error::none);
    const auto economic_before = economic_source.snapshot();
    for (long budget = 0; budget < 2000; ++budget) {
        remaining = budget;
        try {
            auto copy = economic_source.clone(); remaining = -1;
            CHECK(equal(economic_before, copy->snapshot()));
            CHECK(copy->execute({2, 1, {}, {{{1, 1}}, {}}}).error == eco::Error::history_full);
            success = true;
        } catch (const std::bad_alloc&) { remaining = -1; }
        CHECK(equal(economic_before, economic_source.snapshot()));
        if (success) break;
    }
    CHECK(success);
    success = false;
    for (long budget = 0; budget < 2000; ++budget) {
        auto shop = make_shop();
        CHECK(shop.purchase({41, 100, 1, 1}).error == commerce::Error::none);
        const auto before = shop.snapshot();
        remaining = budget;
        try {
            const auto result = shop.purchase({42, 100, 2, 1}); remaining = -1;
            CHECK(result.error == commerce::Error::none && result.receipt);
            CHECK(shop.economy().wallet().balance(1) == 200 && shop.snapshot().purchases.size() == 2);
            success = true;
        } catch (const std::bad_alloc&) { remaining = -1; CHECK(equal(before, shop.snapshot())); }
        if (success) break;
    }
    CHECK(success);
    auto source = make_shop(); CHECK(source.purchase({42, 100, 2, 1}).error == commerce::Error::none);
    const auto snapshot = source.snapshot();
    success = false;
    for (long budget = 0; budget < 2000; ++budget) {
        auto target = make_shop(); const auto before = target.snapshot();
        remaining = budget;
        try {
            const auto result = target.restore(snapshot); remaining = -1;
            CHECK(result == commerce::Error::none && equal(snapshot, target.snapshot())); success = true;
        } catch (const std::bad_alloc&) { remaining = -1; CHECK(equal(before, target.snapshot())); }
        if (success) break;
    }
    CHECK(success);
    success = false;
    for (long budget = 0; budget < 2000; ++budget) {
        std::string output = "keep"; remaining = budget;
        try {
            const auto result = commerce::encode_archive(snapshot, output); remaining = -1;
            CHECK(result == commerce::ArchiveError::none && output != "keep"); success = true;
        } catch (const std::bad_alloc&) { remaining = -1; CHECK(output == "keep"); }
        if (success) break;
    }
    CHECK(success);
    std::string encoded; CHECK(commerce::encode_archive(snapshot, encoded) == commerce::ArchiveError::none);
    success = false;
    for (long budget = 0; budget < 2000; ++budget) {
        commerce::Snapshot output; const auto before = output; remaining = budget;
        try {
            const auto result = commerce::decode_archive(encoded, output); remaining = -1;
            CHECK(result == commerce::ArchiveError::none && equal(snapshot, output)); success = true;
        } catch (const std::bad_alloc&) { remaining = -1; CHECK(equal(before, output)); }
        if (success) break;
    }
    CHECK(success);
    std::cout << "Allocation-failure sweeps preserve clone, shop assets, purchases, restore and codec outputs\n";
}
