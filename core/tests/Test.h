// Tiny dependency-free test harness (no network needed to build tests).
#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& Registry() {
    static std::vector<Case> r;
    return r;
}
inline int& Failures() {
    static int f = 0;
    return f;
}

struct Reg {
    Reg(const char* n, std::function<void()> f) { Registry().push_back({n, std::move(f)}); }
};

}  // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                       \
    static void name();                                                  \
    static test::Reg TEST_CAT(reg_, name)(#name, name);                  \
    static void name()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            ++test::Failures();                                                           \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond << "\n"; \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        auto va = (a);                                                                              \
        auto vb = (b);                                                                              \
        if (!(va == vb)) {                                                                          \
            ++test::Failures();                                                                     \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #a " == " #b "  (" << va \
                      << " vs " << vb << ")\n";                                                     \
        }                                                                                           \
    } while (0)

#define CHECK_OK(st)                                                                                     \
    do {                                                                                                 \
        auto s_ = (st);                                                                                  \
        if (!s_.ok) {                                                                                    \
            ++test::Failures();                                                                          \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #st " -> " << s_.error << "\n"; \
        }                                                                                                \
    } while (0)

#define CHECK_ERR(st) CHECK(!(st).ok)
