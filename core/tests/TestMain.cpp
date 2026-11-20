#include "Test.h"

int main() {
    int failedCases = 0;
    for (auto& c : test::Registry()) {
        int before = test::Failures();
        c.fn();
        bool ok = test::Failures() == before;
        if (!ok) ++failedCases;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << c.name << "\n";
    }
    std::cout << "\n" << test::Registry().size() - failedCases << "/" << test::Registry().size()
              << " tests passed\n";
    return failedCases == 0 ? 0 : 1;
}
