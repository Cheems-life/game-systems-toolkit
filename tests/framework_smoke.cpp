#include <designer/core/version.hpp>
#include <iostream>

int main() {
    // An explicit check remains active in Release builds (unlike assert).
    if (designer::core::version().empty()) {
        std::cerr << "The public library must expose a non-empty version.\n";
        return 1;
    }
    return 0;
}
