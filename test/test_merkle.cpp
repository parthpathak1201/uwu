#include "merkle.hpp"
#include "sha256.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using uwu::FileLeaf;
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

static void check(bool cond, const std::string& msg) {
    if (!cond) throw std::runtime_error(msg);
}

// --- Tests ---

static void empty_tree() {
    Value t = uwu::build_merkle_tree({});
    check(uwu::tree_root(t) == uwu::sha256_hex(""), "empty root");
    check(uwu::tree_leaves(t).empty(), "empty leaves");
}

static void single_leaf() {
    FileLeaf f{"a.txt", "h1"};
    Value t = uwu::build_merkle_tree({f});
    check(uwu::tree_root(t) == "h1", "single leaf root = its hash");
    check(uwu::tree_leaves(t).size() == 1, "one leaf");
    check(uwu::tree_leaves(t)[0].path == "a.txt", "leaf path");
}

static void two_leaves() {
    // root = SHA-256(hash_a + hash_b), level-1-node-0 == root.
    FileLeaf a{"a", "1111"}, b{"b", "2222"};
    Value t = uwu::build_merkle_tree({a, b});
    const str expected = uwu::sha256_hex("11112222");
    check(uwu::tree_root(t) == expected, "two-leaf root");
    const Value* internal = t.get("nodes")->get("internal");
    check(internal->get("level-1-node-0")->string == expected, "level-1-node-0");
}

static void odd_leaves_promote() {
    // 3 leaves: (a,b) pair + c promotes to level 2, then level 2 pairs.
    FileLeaf a{"a", "0001"}, b{"b", "0002"}, c{"c", "0003"};
    Value t = uwu::build_merkle_tree({a, b, c});
    const str ab = uwu::sha256_hex("00010002");
    check(uwu::tree_root(t) == uwu::sha256_hex(ab + "0003"), "3-leaf root");
    const Value* internal = t.get("nodes")->get("internal");
    check(internal->get("level-1-node-0")->string == ab, "level-1 pair");
    check(internal->get("level-2-node-0")->string == uwu::tree_root(t),
          "level-2-node-0 == root");
    // c is not paired at level 1.
    check(internal->get("level-1-node-1") == nullptr, "no level-1 node 1");
}

static void four_leaves() {
    FileLeaf a{"a", "0001"}, b{"b", "0002"}, c{"c", "0003"}, d{"d", "0004"};
    Value t = uwu::build_merkle_tree({a, b, c, d});
    const str ab = uwu::sha256_hex("00010002");
    const str cd = uwu::sha256_hex("00030004");
    check(uwu::tree_root(t) == uwu::sha256_hex(ab + cd), "4-leaf root");
}

static void order_independent() {
    FileLeaf a{"a", "0001"}, b{"b", "0002"}, c{"c", "0003"};
    Value t1 = uwu::build_merkle_tree({a, b, c});
    Value t2 = uwu::build_merkle_tree({c, a, b});
    check(uwu::tree_root(t1) == uwu::tree_root(t2), "order-independent root");
    check(uwu::json::stringify(t1) == uwu::json::stringify(t2), "order-independent tree");
}

static void leaves_roundtrip() {
    FileLeaf a{"z", "hash_z"}, b{"m", "hash_m"}, c{"a", "hash_a"};
    Value t = uwu::build_merkle_tree({a, b, c});
    auto leaves = uwu::tree_leaves(t);
    check(leaves.size() == 3, "leaf count");
    // Ascending path order from std::map.
    check(leaves[0].path == "a" && leaves[0].hash == "hash_a", "sorted 0");
    check(leaves[1].path == "m" && leaves[1].hash == "hash_m", "sorted 1");
    check(leaves[2].path == "z" && leaves[2].hash == "hash_z", "sorted 2");
}

static void changed_paths_works() {
    FileLeaf a{"a", "old"}, b{"b", "same"}, c{"c", "gone"};
    FileLeaf a2{"a", "new"}, b2{"b", "same"}, d2{"d", "added"};
    Value t1 = uwu::build_merkle_tree({a, b, c});
    Value t2 = uwu::build_merkle_tree({a2, b2, d2});
    auto changed = uwu::changed_paths(t1, t2);
    // a modified, c deleted, d added; b unchanged.
    check(changed.size() == 3, "changed count");
    check(changed[0] == "a", "a modified");
    check(changed[1] == "c", "c deleted");
    check(changed[2] == "d", "d added");
}

static void tombstone_semantics() {
    check(uwu::is_tombstone(uwu::TOMBSTONE_HASH), "tombstone marker");
    check(!uwu::is_tombstone(uwu::sha256_hex("x")), "real hash is not tombstone");
}

int main() {
    std::cout << "merkle tests\n";
    std::cout << "------------\n";
    TEST(empty_tree);
    TEST(single_leaf);
    TEST(two_leaves);
    TEST(odd_leaves_promote);
    TEST(four_leaves);
    TEST(order_independent);
    TEST(leaves_roundtrip);
    TEST(changed_paths_works);
    TEST(tombstone_semantics);
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
