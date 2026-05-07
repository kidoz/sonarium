#include <iostream>

#include "core/version.hpp"

int main() {
    auto const v = sonarium::core::current_version();
    std::cout << sonarium::core::product_name() << " server " << v.major << '.' << v.minor << '.'
              << v.patch << " (HLS/CMAF) — TODO: wire Atria HTTP server\n";
    return 0;
}
