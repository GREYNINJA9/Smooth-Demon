#include <iostream>
#include "version.hpp"

int main() {
    std::cout
        << "==============================================\n"
        << "  SmoothDemon v" << smoothdemon::version_string << "\n"
        << "  Gaming FPS Booster / System Monitor\n"
        << "==============================================\n"
        << "\n"
        << "Phase 0: environment skeleton built successfully.\n"
        << "Run './build/tests/test_environment' to verify the host.\n";

    return 0;
}
