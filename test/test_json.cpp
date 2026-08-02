#include "json.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using uwu::json::Value;

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

static Value parse_str(const std::string& s) {
    return uwu::json::parse(s);
}

static std::string ser(const Value& v) {
    return uwu::json::stringify(v);
}

static void roundtrip(const std::string& input) {
    std::string out = ser(parse_str(input));
    if (out != input)
        throw std::runtime_error("roundtrip: \"" + input + "\" -> \"" + out + "\"");
}

// --- Tests ---

static void scalars() {
    if (!ser(Value::make_null()).empty() && parse_str("null").kind != uwu::json::Kind::Null)
        throw std::runtime_error("null");
    if (!parse_str("true").boolean || parse_str("false").boolean)
        throw std::runtime_error("bool");
    if (parse_str("42").number != 42)
        throw std::runtime_error("int");
    if (parse_str("-7").number != -7)
        throw std::runtime_error("negative");
}

static void string_basics() {
    Value v = parse_str("\"hello\"");
    if (v.string != "hello") throw std::runtime_error("basic string");
    if (parse_str("\"\\n\\t\\\"\\\\\\/\"").string != "\n\t\"\\/")
        throw std::runtime_error("escapes");
}

static void string_unicode() {
    if (parse_str("\"\\u0041\"").string != "A")
        throw std::runtime_error("ascii escape");
    if (parse_str("\"\\u00e9\"").string != "\xC3\xA9")
        throw std::runtime_error("latin-1 escape");
    // Surrogate pair U+1F600 (grinning face)
    if (parse_str("\"\\ud83d\\ude00\"").string != "\xF0\x9F\x98\x80")
        throw std::runtime_error("surrogate pair");
    // Raw UTF-8 bytes pass through untouched
    if (parse_str("\"\xF0\x9F\x98\x80\"").string != "\xF0\x9F\x98\x80")
        throw std::runtime_error("raw utf-8");
}

static void arrays() {
    Value v = parse_str("[1,2,3]");
    if (v.array.size() != 3 || v.array[0].number != 1 || v.array[2].number != 3)
        throw std::runtime_error("int array");
    if (parse_str("[]").array.size() != 0) throw std::runtime_error("empty array");
    Value nested = parse_str("[[1],[2]]");
    if (nested.array.size() != 2 || nested.array[1].array[0].number != 2)
        throw std::runtime_error("nested array");
}

static void objects() {
    Value v = parse_str("{\"a\":1,\"b\":\"x\"}");
    if (!v.is_object()) throw std::runtime_error("not object");
    if (v.get("a")->number != 1) throw std::runtime_error("get a");
    if (v.get("b")->string != "x") throw std::runtime_error("get b");
    if (v.get("missing") != nullptr) throw std::runtime_error("get missing");
    if (parse_str("{}").object.size() != 0) throw std::runtime_error("empty object");
}

static void keys_sorted_deterministic() {
    // Sorted output is load-bearing for commit hashing.
    if (ser(parse_str("{\"z\":1,\"a\":2}")) != "{\"a\":2,\"z\":1}")
        throw std::runtime_error("not sorted");
    if (ser(parse_str("{\"a\":1,\"b\":2}")) != ser(parse_str("{\"b\":2,\"a\":1}")))
        throw std::runtime_error("order-sensitive serialization");
}

static void duplicate_keys_last_wins() {
    if (parse_str("{\"a\":1,\"a\":2}").get("a")->number != 2)
        throw std::runtime_error("duplicate keys");
}

static void whitespace_tolerance() {
    if (ser(parse_str("  { \"a\" : [ 1 , 2 ] }  ")) != "{\"a\":[1,2]}")
        throw std::runtime_error("whitespace");
}

static void compact_stringify() {
    Value root = Value::make_object();
    root.object["root"] = Value::make_string("v");
    root.object["n"] = Value::make_number(3);
    if (ser(root) != "{\"n\":3,\"root\":\"v\"}")
        throw std::runtime_error("compact output");
}

static void builder_roundtrip() {
    Value root = Value::make_object();
    root.object["merkle_root"] = Value::make_string("abc123");
    Value nodes = Value::make_object();
    nodes.object["src/main.rs"] = Value::make_string("h1");
    root.object["nodes"] = std::move(nodes);
    root.object["snapshot"] = Value::make_bool(true);
    root.object["timestamp"] = Value::make_number(1234567890);
    std::string s = ser(root);
    Value back = parse_str(s);
    if (back.get("nodes")->get("src/main.rs")->string != "h1")
        throw std::runtime_error("deep get");
    if (ser(back) != s) throw std::runtime_error("builder roundtrip unstable");
}

// --- Error cases ---

static void errors() {
    const char* bad[] = {
        "", "{", "}", "[", "]", "{\"a\"}", "{\"a\":}", "[1,]",
        "\"unterminated", "'single'", "01", "1.5", "1e3", "tru", "nul",
        "{\"a\":1} trailing", "{\"a\" 1}",
    };
    for (const char* s : bad) {
        try {
            (void)uwu::json::parse(s);
            throw std::runtime_error(std::string("accepted invalid: \"") + s + "\"");
        } catch (const std::runtime_error&) {
            // expected
        }
    }
}

static void unicode_escapes_serialize() {
    // Control chars must be escaped on write and round-trip back.
    Value v = Value::make_string(std::string("a\0b", 3));
    std::string s = ser(v);
    if (s != "\"a\\u0000b\"") throw std::runtime_error("control char escape");
    if (parse_str(s).string != std::string("a\0b", 3))
        throw std::runtime_error("control char roundtrip");
}

static void rfc_example() {
    // A real-ish document end to end.
    const char* doc = "{\"sha\":\"abcd\",\"parent_sha\":[\"x\",\"y\"],\"msg\":\"hi\\n\"}";
    Value v = parse_str(doc);
    if (v.get("parent_sha")->array.size() != 2) throw std::runtime_error("rfc parents");
    if (ser(v) != "{\"msg\":\"hi\\n\",\"parent_sha\":[\"x\",\"y\"],\"sha\":\"abcd\"}")
        throw std::runtime_error("rfc stringify");
}

int main() {
    std::cout << "json tests\n";
    std::cout << "-----------\n";
    TEST(scalars);
    TEST(string_basics);
    TEST(string_unicode);
    TEST(arrays);
    TEST(objects);
    TEST(keys_sorted_deterministic);
    TEST(duplicate_keys_last_wins);
    TEST(whitespace_tolerance);
    TEST(compact_stringify);
    TEST(builder_roundtrip);
    TEST(errors);
    TEST(unicode_escapes_serialize);
    TEST(rfc_example);
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
