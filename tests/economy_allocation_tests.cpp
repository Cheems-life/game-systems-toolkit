#include "economy_test_support.hpp"
#include <new>

// This executable alone injects bad_alloc into each successive allocation point.
// Disabled while creating fixtures and inspecting state.
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
    bool reached_success = false;
    const auto request = purchase();
    for (long budget = 0; budget < 1000; ++budget) {
        auto economy = make_economy();
        const auto before = economy.snapshot();
        remaining = budget;
        try {
            auto result = economy.execute(request);
            remaining = -1;
            CHECK(result.error == eco::Error::none && result.receipt && economy.history_size() == 1);
            reached_success = true;
        } catch (const std::bad_alloc&) {
            remaining = -1;
            CHECK(equal(before, economy.snapshot()));
        }
        if (reached_success) break;
    }
    CHECK(reached_success);
    auto source = make_economy();
    CHECK(source.execute(request).error == eco::Error::none);
    const auto snapshot = source.snapshot();
    reached_success = false;
    for (long budget = 0; budget < 1000; ++budget) {
        auto target = make_economy();
        const auto before = target.snapshot();
        remaining = budget;
        try {
            const auto result = target.restore(snapshot);
            remaining = -1;
            CHECK(result == eco::Error::none && equal(snapshot, target.snapshot()));
            reached_success = true;
        } catch (const std::bad_alloc&) {
            remaining = -1;
            CHECK(equal(before, target.snapshot()));
        }
        if (reached_success) break;
    }
    CHECK(reached_success);
    reached_success = false;
    for (long budget = 0; budget < 1000; ++budget) {
        std::string output = "keep";
        remaining = budget;
        try {
            const auto result = eco::encode_archive(snapshot, output);
            remaining = -1;
            CHECK(result == eco::ArchiveError::none && output != "keep");
            reached_success = true;
        } catch (const std::bad_alloc&) {
            remaining = -1;
            CHECK(output == "keep");
        }
        if (reached_success) break;
    }
    CHECK(reached_success);
    std::string encoded;
    CHECK(eco::encode_archive(snapshot, encoded) == eco::ArchiveError::none);
    reached_success = false;
    for (long budget = 0; budget < 1000; ++budget) {
        auto target = make_economy();
        auto output = target.snapshot();
        const auto before = output;
        remaining = budget;
        try {
            const auto result = eco::decode_archive(encoded, output);
            remaining = -1;
            CHECK(result == eco::ArchiveError::none && equal(output, snapshot));
            reached_success = true;
        } catch (const std::bad_alloc&) {
            remaining = -1;
            CHECK(equal(output, before));
        }
        if (reached_success) break;
    }
    CHECK(reached_success);
    std::cout << "Allocation-failure sweeps preserve transactions, restore and archive output\n";
}
