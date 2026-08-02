#include "regex.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

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

static void assert_match(const uwu::Regex& r, const std::string& text) {
    if (!r.match(text))
        throw std::runtime_error("expected match(\"" + text + "\") but got false");
}

static void assert_not_match(const uwu::Regex& r, const std::string& text) {
    if (r.match(text))
        throw std::runtime_error("expected no match(\"" + text + "\") but got true");
}

static void assert_search(const uwu::Regex& r, const std::string& text) {
    if (!r.search(text))
        throw std::runtime_error("expected search(\"" + text + "\") but got false");
}

static void assert_not_search(const uwu::Regex& r, const std::string& text) {
    if (r.search(text))
        throw std::runtime_error("expected no search(\"" + text + "\") but got true");
}

// --- Tests ---

static void literal_exact() {
    uwu::Regex r("hello");
    assert_match(r, "hello");
    assert_not_match(r, "hello!");
    assert_not_match(r, "hell");
    assert_not_match(r, "xhello");
    // search finds it anywhere
    assert_search(r, "hello");
    assert_search(r, "say hello!");
    assert_not_search(r, "hxllo");
}

static void dot_any_char() {
    uwu::Regex r("h.llo");
    assert_match(r, "hallo");
    assert_match(r, "hxllo");
    assert_match(r, "h llo");
    assert_match(r, std::string("h\0llo", 5)); // embedded null
    assert_not_match(r, "h\nllo"); // dot matches except newline
    assert_not_search(r, "h\nllo");
}

static void star_zero_or_more() {
    uwu::Regex r("ab*c");
    assert_match(r, "ac");
    assert_match(r, "abc");
    assert_match(r, "abbc");
    assert_match(r, "abbbc");
    assert_not_match(r, "abx");
}

static void plus_one_or_more() {
    uwu::Regex r("ab+c");
    assert_not_match(r, "ac");
    assert_match(r, "abc");
    assert_match(r, "abbc");
}

static void qmark_optional() {
    uwu::Regex r("ab?c");
    assert_match(r, "ac");
    assert_match(r, "abc");
    assert_not_match(r, "abbc");
}

static void alternation() {
    uwu::Regex r("a|b");
    assert_match(r, "a");
    assert_match(r, "b");
    assert_not_match(r, "c");
    assert_search(r, "xa");
    assert_search(r, "bx");
}

static void alternation_three() {
    uwu::Regex r("a|b|c");
    assert_match(r, "a");
    assert_match(r, "b");
    assert_match(r, "c");
    assert_not_match(r, "d");
}

static void grouping() {
    uwu::Regex r("(ab)+");
    assert_not_match(r, "a");
    assert_match(r, "ab");
    assert_match(r, "abab");
    assert_not_match(r, "aba");
}

static void char_class() {
    uwu::Regex r("[abc]");
    assert_match(r, "a");
    assert_match(r, "b");
    assert_match(r, "c");
    assert_not_match(r, "d");
    assert_search(r, "xa");
    assert_not_search(r, "dx");
}

static void char_class_range() {
    uwu::Regex r("[a-z]");
    assert_match(r, "m");
    assert_match(r, "a");
    assert_match(r, "z");
    assert_not_match(r, "A");
    assert_not_match(r, "3");
}

static void char_class_negated() {
    uwu::Regex r("[^abc]");
    assert_match(r, "d");
    assert_not_match(r, "a");
    assert_not_match(r, "b");
    assert_match(r, "1");
}

static void anchors_caret() {
    uwu::Regex r("^hello");
    assert_match(r, "hello");
    assert_not_match(r, "xhello");
    // search with ^: should NOT find in middle
    assert_search(r, "hello");
    assert_not_search(r, "xhello");
}

static void anchors_dollar() {
    uwu::Regex r("world$");
    assert_match(r, "world");
    assert_not_match(r, "worldx");
    assert_not_match(r, "xworld");
    assert_search(r, "hello world");
    assert_not_search(r, "worlds");
}

static void anchors_both() {
    uwu::Regex r("^exact$");
    assert_match(r, "exact");
    assert_not_match(r, "not exact");
    assert_not_match(r, "exactly");
    assert_not_match(r, "very exact");
    assert_search(r, "exact");
    assert_not_search(r, "exactly");
}

static void escape_d() {
    uwu::Regex r("\\d+");
    assert_search(r, "hello 42 world");
    assert_search(r, "123");
    assert_not_search(r, "abc");
    assert_match(r, "42");
}

static void escape_w() {
    uwu::Regex r("\\w+");
    assert_match(r, "hello");
    assert_match(r, "test_123");
    assert_not_match(r, "");
    assert_not_search(r, "   ");
}

static void escape_s() {
    uwu::Regex r("\\s+");
    assert_match(r, "   ");
    assert_match(r, "\t\n");
    assert_not_search(r, "abc");
}

static void escape_D() {
    uwu::Regex r("\\D+");
    assert_match(r, "abc");
    assert_not_match(r, "123");
    assert_search(r, "a1b2c");
}

static void complex_pattern() {
    // ponytail: no {n,m} quantifier yet, use manual repetition
    uwu::Regex r("^(https?://)?[a-z]+\\.[a-z][a-z]([a-z])?(/.*)?$");
    assert_match(r, "example.com");
    assert_match(r, "https://example.com");
    assert_match(r, "http://test.org/path");
    assert_not_match(r, "not-a-domain");
    assert_not_search(r, "not-a-domain");
}

static void empty_pattern() {
    uwu::Regex r("");
    assert_match(r, "");
    assert_not_match(r, "a");
}

static void dot_star() {
    uwu::Regex r(".*");
    assert_match(r, "");
    assert_match(r, "anything");
}

static void escape_literals() {
    uwu::Regex r("a\\.b");
    assert_match(r, "a.b");
    assert_not_match(r, "axb");
}

// --- Compile-time usage demo for uwuignore patterns ---
static void uwuignore_demo() {
    uwu::Regex dot_o("\\.o$");
    assert_search(dot_o, "file.o");
    assert_not_search(dot_o, "file.cpp");

    uwu::Regex build_dir("^build/");
    assert_search(build_dir, "build/foo.o");
    assert_not_search(build_dir, "src/build.c");

    uwu::Regex node_modules("node_modules/");
    assert_search(node_modules, "project/node_modules/express/index.js");
    assert_not_search(node_modules, "src/node.js");

    uwu::Regex pycache("__pycache__/");
    assert_search(pycache, "project/__pycache__/foo.pyc");
    assert_not_search(pycache, "src/pycache_test.py");
}

int main() {
    std::cout << "regex tests\n";
    std::cout << "-----------\n";

    TEST(literal_exact);
    TEST(dot_any_char);
    TEST(star_zero_or_more);
    TEST(plus_one_or_more);
    TEST(qmark_optional);
    TEST(alternation);
    TEST(alternation_three);
    TEST(grouping);
    TEST(char_class);
    TEST(char_class_range);
    TEST(char_class_negated);
    TEST(anchors_caret);
    TEST(anchors_dollar);
    TEST(anchors_both);
    TEST(escape_d);
    TEST(escape_w);
    TEST(escape_s);
    TEST(escape_D);
    TEST(complex_pattern);
    TEST(empty_pattern);
    TEST(dot_star);
    TEST(escape_literals);
    TEST(uwuignore_demo);

    std::cout << "-----------\n";
    std::cout << tests_passed << "/" << tests_run << " passed\n";

    return tests_passed == tests_run ? 0 : 1;
}
