// 配置异常测试：逐点注入分配失败，覆盖成功与错误诊断路径；不会替换调用方已有配置包。
#include "test_support.hpp"
#include <new>
namespace { long remaining = -1; }
void* operator new(std::size_t size) {
    if (remaining == 0) throw std::bad_alloc();
    if (remaining > 0) --remaining;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
int main() {
    namespace c = designer::configuration;
    const auto bytes = fixture();
    const auto registry = designer::quests::default_objectives();
    auto current = c::decode(bytes, registry).bundle; CHECK(current);
    const auto* original = current.get();
    const auto bad = replace(bytes, "\"item\": 1001", "\"item\": 9999");
    auto v2 = replace(bytes, "\"schema_version\": 1", "\"schema_version\": 2");
    v2 = replace(v2, "\"target\": 3", "\"target\": 3, \"binding\": {\"fact_type\":100,\"subject\":1001,\"input\":\"quantity\"}");
    v2 = replace(v2, "\"target\": 1}", "\"target\": 1, \"binding\": null}");
    v2 = replace(v2, "\"target\": 10", "\"target\": 10, \"binding\": null");
    for (const auto& input : std::vector<std::string>{bytes, v2, bad, "{", "{\"x\":1,\"x\":2}"}) {
        const bool valid = input == bytes || input == v2;
        bool finished = false;
        for (long budget = 0; budget < 6000; ++budget) {
            remaining = budget;
            try {
                auto result = c::decode(input, registry);
                remaining = -1;
                CHECK(valid ? result.bundle && !result.error : !result.bundle && result.error.has_value());
                finished = true;
            } catch (const std::bad_alloc&) { remaining = -1; }
            CHECK(current.get() == original && current->products().find(100));
            CHECK(current->quests().find(1)->objectives[0].target == 3);
            if (finished) break;
        }
        CHECK(finished);
    }
    std::cout << "Configuration allocation failure sweeps passed\n";
}
