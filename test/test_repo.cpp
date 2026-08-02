#include "repo.hpp"
#include "sha256.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using uwu::IndexEntry;
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

// Runs inside a fresh temp sandbox so relative ".uwu" paths are safe.
struct Sandbox {
    std::filesystem::path old;
    std::filesystem::path dir;

    Sandbox() {
        old = std::filesystem::current_path();
        dir = old / ("uwu-test-" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir);
        std::filesystem::current_path(dir);
    }
    ~Sandbox() {
        std::filesystem::current_path(old);
        std::filesystem::remove_all(dir);
    }
};

static void write(const std::string& path, const std::string& content) {
    std::ofstream(path) << content;
}

// --- Tests ---

static void init_creates_layout() {
    Sandbox sb;
    check(!uwu::repo_exists(), "no repo before init");
    uwu::init_repo();
    check(uwu::repo_exists(), "repo after init");
    check(uwu::current_branch() == "main", "HEAD on main");
    check(uwu::head_sha().empty(), "no commits yet");
}

static void index_roundtrip() {
    Sandbox sb;
    uwu::init_repo();
    Vec<IndexEntry> in = {{"a.txt", "hash1"}, {"src/b.rs", "hash2"}};
    uwu::write_index(in);
    auto out = uwu::read_index();
    check(out.size() == 2, "index size");
    check(out[0].path == "a.txt" && out[0].hash == "hash1", "entry 0");
    check(out[1].path == "src/b.rs" && out[1].hash == "hash2", "entry 1");
}

static void refs_roundtrip() {
    Sandbox sb;
    uwu::init_repo();
    check(uwu::resolve_ref("refs/heads/main").empty(), "main has no sha yet");
    uwu::update_ref("refs/heads/main", "deadbeef");
    check(uwu::resolve_ref("refs/heads/main") == "deadbeef", "resolve_ref");
    uwu::update_ref("refs/heads/dev", "cafebabe");
    auto branches = uwu::list_branches();
    check(branches.size() == 2 && branches[0] == "dev" && branches[1] == "main",
          "branches sorted");
}

static void head_detached_handling() {
    Sandbox sb;
    uwu::init_repo();
    uwu::set_head("abc123");
    check(uwu::current_branch().empty(), "detached -> no branch");
    check(uwu::head_sha() == "abc123", "detached sha");
    uwu::set_head("refs/heads/main");
    check(uwu::current_branch() == "main", "back on branch");
}

static void blob_roundtrip() {
    Sandbox sb;
    uwu::init_repo();
    const std::string text = "hello world, this gets compressed\n" + std::string(5000, 'x');
    std::vector<char> data(text.begin(), text.end());
    uwu::store_blob("abc", "src/main.rs", data);
    check(uwu::blob_exists("abc", "src/main.rs"), "blob exists");
    auto got = uwu::load_blob("abc", "src/main.rs");
    check(std::string(got.begin(), got.end()) == text, "blob roundtrip");
    check(!uwu::blob_exists("abc", "nope.txt"), "missing blob");
    check(!uwu::blob_exists("zzz", "src/main.rs"), "missing commit");
}

static void commit_meta_tree_roundtrip() {
    Sandbox sb;
    uwu::init_repo();
    uwu::json::Value meta = uwu::json::Value::make_object();
    meta.object["sha"] = uwu::json::Value::make_string("abc");
    meta.object["snapshot"] = uwu::json::Value::make_bool(true);
    meta.object["merkle_root"] = uwu::json::Value::make_string("r1");
    uwu::json::Value tree = uwu::json::Value::make_object();
    tree.object["root"] = uwu::json::Value::make_string("r1");
    uwu::write_commit_meta("abc", meta);
    uwu::write_commit_tree("abc", tree);
    check(uwu::commit_exists("abc"), "commit exists");
    check(uwu::commit_is_snapshot("abc"), "snapshot flag");
    check(uwu::commit_merkle_root("abc") == "r1", "merkle root");
    check(uwu::commit_parents("abc").empty(), "no parents");
    check(!uwu::commit_exists("nope"), "unknown commit");
}

static void list_working_files_basic() {
    Sandbox sb;
    uwu::init_repo();
    write("main.rs", "fn main() {}");
    write("README.md", "# uwu\n");
    std::filesystem::create_directories("src");
    write("src/lib.rs", "pub fn x() {}");
    IgnoreMatcher ig({});
    auto files = uwu::list_working_files(ig);
    check(files.size() == 3, "found all 3 files");
    check(files[0] == "README.md" && files[1] == "main.rs" && files[2] == "src/lib.rs",
          "sorted, .uwu excluded");
}

static void list_working_files_ignores() {
    Sandbox sb;
    uwu::init_repo();
    write("keep.rs", "x");
    write("drop.log", "y");
    std::filesystem::create_directories("build");
    write("build/obj.o", "z");
    write("src.rs", "keep");
    IgnoreMatcher ig({"\\.log$", "build"});
    auto files = uwu::list_working_files(ig);
    check(files.size() == 2, "ignored files and dirs pruned");
    check(files[0] == "keep.rs" && files[1] == "src.rs", "remaining files");
}

static void hash_file_works() {
    Sandbox sb;
    uwu::init_repo();
    write("x", "abc");
    check(uwu::hash_file("x") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "hash matches sha256(\"abc\")");
    check(uwu::hash_file("missing").empty(), "missing file -> empty");
}

int main() {
    std::cout << "repo tests\n";
    std::cout << "----------\n";
    TEST(init_creates_layout);
    TEST(index_roundtrip);
    TEST(refs_roundtrip);
    TEST(head_detached_handling);
    TEST(blob_roundtrip);
    TEST(commit_meta_tree_roundtrip);
    TEST(list_working_files_basic);
    TEST(list_working_files_ignores);
    TEST(hash_file_works);
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
