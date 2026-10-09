#include <designer/core/version.hpp>
#include <iostream>

int main() {
    std::cout << "Designer Toolkit " << designer::core::version() << '\n';
    std::cout << "Framework ready. Inventory rules are the next milestone.\n";
}
