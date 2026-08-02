#include "compress.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

static void assert_roundtrip(const std::string& input) {
    auto compressed = uwu::compress(input);
    auto decompressed = uwu::decompress(compressed);
    if (decompressed != input) {
        throw std::runtime_error("roundtrip mismatch: got \"" + decompressed +
                                 "\" expected \"" + input + "\"");
    }
}

static void assert_roundtrip_binary(const std::vector<char>& input) {
    auto compressed = uwu::compress(input.data(), input.size());
    auto decompressed = uwu::decompress(compressed.data(), compressed.size());
    if (decompressed.size() != input.size() ||
        memcmp(decompressed.data(), input.data(), input.size()) != 0) {
        throw std::runtime_error("binary roundtrip mismatch");
    }
}

// --- Tests ---

static void empty_string() {
    assert_roundtrip("");
}

static void single_byte() {
    assert_roundtrip("x");
    assert_roundtrip("A");
    assert_roundtrip(".");
}

static void short_ascii() {
    assert_roundtrip("hello");
    assert_roundtrip("world");
    assert_roundtrip("hello world");
}

static void repetitive() {
    assert_roundtrip("aaaaa");
    assert_roundtrip("abcabcabc");
    assert_roundtrip("1111111111");
    assert_roundtrip("ababababab");
}

static void long_repetitive() {
    std::string s;
    for (int i = 0; i < 1000; i++) s += "hello world ";
    assert_roundtrip(s);
}

static void all_printable() {
    std::string s;
    for (int c = 32; c < 127; c++) s += (char)c;
    assert_roundtrip(s);
}

static void binary_data() {
    std::vector<char> data;
    for (int i = 0; i < 256; i++) data.push_back((char)i);
    for (int i = 0; i < 256; i++) data.push_back((char)(255 - i));
    assert_roundtrip_binary(data);
}

static void null_bytes() {
    std::string s;
    s += '\0';
    s += "hello";
    s += '\0';
    s += "world";
    s += '\0';
    assert_roundtrip(s);
}

static void cpp_source() {
    std::string s = R"(
#include <iostream>

int main() {
    std::cout << "Hello, World!" << std::endl;
    return 0;
}
)";
    assert_roundtrip(s);
}

static void markdown_text() {
    std::string s = R"(
# uwu — Version Control System

uwu is a minimal Git clone that tracks file snapshots as a DAG of commits.
Each commit stores file state as deltas from its parent, with merkle trees
enabling fast change detection.

## Features

- init, add, status, commit, checkout, merge
- DAG-based commit history
- Delta compression for storage efficiency
- Merkle tree for fast change detection
- Regex-based .uwuignore engine
- LZ77 + Huffman compression
)";
    assert_roundtrip(s);
}

static void compression_ratio_repetitive() {
    // Repetitive data should compress well
    std::string input(10000, 'a');
    auto compressed = uwu::compress(input);
    double ratio = (double)compressed.size() / input.size();
    if (ratio > 0.5) {
        throw std::runtime_error("repetitive data compression ratio too high: " +
                                 std::to_string(ratio));
    }
    // Verify roundtrip still works
    auto decompressed = uwu::decompress(compressed);
    if (decompressed != input) {
        throw std::runtime_error("repetitive roundtrip failed");
    }
}

static void large_random_like() {
    std::string s;
    // Mix of repetitive and unique content
    for (int i = 0; i < 500; i++) {
        s += "function foo_" + std::to_string(i) + "() { return " + std::to_string(i * 2) + "; }\n";
    }
    assert_roundtrip(s);
}

int main() {
    std::cout << "compress tests\n";
    std::cout << "--------------\n";

    TEST(empty_string);
    TEST(single_byte);
    TEST(short_ascii);
    TEST(repetitive);
    TEST(long_repetitive);
    TEST(all_printable);
    TEST(binary_data);
    TEST(null_bytes);
    TEST(cpp_source);
    TEST(markdown_text);
    TEST(compression_ratio_repetitive);
    TEST(large_random_like);

    std::cout << "--------------\n";
    std::cout << tests_passed << "/" << tests_run << " passed\n";

    return tests_passed == tests_run ? 0 : 1;
}
