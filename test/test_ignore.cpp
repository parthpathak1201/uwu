#include "ignore.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using uwu::IgnoreMatcher;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    try { \
        name(); \
        tests_passed++; \
        std::cout << "  PASS  " #name << "\n"; \
    } catch (const std::exception& e) { \
        std::cout << "  FAIL  " #name << ": " << e.what() << "\n"; \
    } \
} while(0)

static void check(bool cond, const std::string& msg) {
    if (!cond) throw std::runtime_error(msg);
}

// --- Tests ---

static void empty_patterns() {
    IgnoreMatcher m({});
    check(!m.ignores("anything.txt"), "no patterns -> nothing ignored");
    check(!m.ignores_dir("src"), "no patterns -> no dirs ignored");
}

static void suffix_pattern() {
    IgnoreMatcher m({"\\.log$"});
    check(m.ignores("a.log"), "a.log ignored");
    check(m.ignores("src/deep/b.log"), "nested .log ignored");
    check(!m.ignores("a.txt"), "a.txt not ignored");
    check(!m.ignores("logic.rs"), "logic.rs must NOT match \\.log$");
}

static void bare_name_pattern() {
    IgnoreMatcher m({"build"});
    check(m.ignores("build"), "build dir ignored");
    check(m.ignores("build/a.o"), "file under build ignored (substring)");
    check(m.ignores_dir("build"), "build dir skipped");
}

static void comments_and_blanks_skipped() {
    IgnoreMatcher m({"# comment", "", "  ", "\t", "\\.tmp$"});
    check(m.ignores("x.tmp"), "real pattern applies");
    check(!m.ignores("x.c"), "comment/blank lines are not patterns");
}

static void any_pattern_wins() {
    IgnoreMatcher m({"\\.o$", "\\.a$"});
    check(m.ignores("foo.o"), "matches .o");
    check(m.ignores("lib.a"), "matches .a");
    check(!m.ignores("foo.rs"), "no match");
}

static void anchored_patterns() {
    IgnoreMatcher m({"^src/"});
    check(m.ignores("src/main.rs"), "matches src/ prefix");
    check(!m.ignores("other/src/main.rs"), "anchored start does not match mid-path");
}

int main() {
    std::cout << "ignore tests\n";
    std::cout << "------------\n";
    TEST(empty_patterns);
    TEST(suffix_pattern);
    TEST(bare_name_pattern);
    TEST(comments_and_blanks_skipped);
    TEST(any_pattern_wins);
    TEST(anchored_patterns);
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
