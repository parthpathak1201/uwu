#include "sha256.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

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

static void assert_hex(const std::string& got, const std::string& expected) {
    if (got != expected)
        throw std::runtime_error("got \"" + got + "\" expected \"" + expected + "\"");
}

// --- Tests ---

// Known-answer vectors from the FIPS 180-4 specification (Appendix B).
static void fips_empty_string() {
    assert_hex(uwu::sha256_hex(""),
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

static void fips_abc() {
    assert_hex(uwu::sha256_hex("abc"),
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void fips_two_block() {
    // 448-bit message: exercises the padding path where data + 0x80 + the
    // 64-bit length spills across two blocks.
    assert_hex(uwu::sha256_hex(
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void fips_million_a() {
    // The classic one-million-'a' vector; stresses chunked processing.
    std::string s(1'000'000, 'a');
    assert_hex(uwu::sha256_hex(s),
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Padding-boundary cases: length where 0x80 lands exactly at the tail of a
// block, one byte over, etc. References computed with `shasum -a 256`.
static void padding_55_bytes() {
    assert_hex(uwu::sha256_hex(std::string(55, 'x')),
               "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
}

static void padding_56_bytes() {
    // 56 bytes: 0x80 fills the 56th byte, length occupies the last 8.
    assert_hex(uwu::sha256_hex(std::string(56, 'y')),
               "4877e564e5e36e367c7c8d59670774becd3350610b6df4c399c9fa9b66da5813");
}

static void padding_57_bytes() {
    // 57 bytes: forces the extra-block padding path (57 + 1 > 56).
    assert_hex(uwu::sha256_hex(std::string(57, 'z')),
               "f7e927f23e3effb6bca2c32a1f94410002a69d21c0b506541c2a08a595be512d");
}

static void padding_63_bytes() {
    // 63 bytes: 0x80 fills the block exactly, then a fresh block for length.
    assert_hex(uwu::sha256_hex(std::string(63, 'q')),
               "9b49777003a4143d8c3f4d3002eb631c6bda8b030a3ccc87f8a21d5f61c89e87");
}

static void all_byte_values() {
    // Every byte value 0..255 exactly once; catches endianness/sign bugs.
    std::string s;
    for (int i = 0; i < 256; i++) s.push_back(static_cast<char>(i));
    assert_hex(uwu::sha256_hex(s),
               "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");
}

static void empty_pointer_ok() {
    // data == nullptr with size == 0 must be legal (common when hashing
    // the contents of an empty file).
    assert_hex(uwu::sha256_hex(nullptr, 0),
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

int main() {
    std::cout << "sha256 tests\n";
    std::cout << "-----------\n";
    TEST(fips_empty_string);
    TEST(fips_abc);
    TEST(fips_two_block);
    TEST(fips_million_a);
    TEST(padding_55_bytes);
    TEST(padding_56_bytes);
    TEST(padding_57_bytes);
    TEST(padding_63_bytes);
    TEST(all_byte_values);
    TEST(empty_pointer_ok);
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
