#include <cstring>

#include "Test.h"

// Usage: cricket_tests [name-substring]   (runs every test whose name contains it)
int main(int argc, char** argv) {
    int failedCases = 0, ran = 0;
    for (auto& c : test::Registry()) {
        if (argc > 1 && !std::strstr(c.name, argv[1])) continue;
        ++ran;
        int before = test::Failures();
        c.fn();
        bool ok = test::Failures() == before;
        if (!ok) ++failedCases;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << c.name << std::endl;
    }
    std::cout << "\n" << ran - failedCases << "/" << ran << " tests passed\n";
    return failedCases == 0 && ran > 0 ? 0 : 1;
}
