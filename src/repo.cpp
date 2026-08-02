#include "repo.hpp"
#include "compress.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>

namespace uwu {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Repo presence / scaffolding
// ---------------------------------------------------------------------------

bool repo_exists() {
    return fs::is_directory(REPO_DIR);
}

void init_repo() {
    fs::create_directories(".uwu/refs/heads");
    fs::create_directories(".uwu/commits");

    const char* user = std::getenv("USER");
    std::ofstream(".uwu/config") << "author=" << (user ? user : "unknown") << '\n'
                                 << "timestamp=" << std::time(nullptr) << '\n';
    std::ofstream(".uwu/HEAD") << "ref: refs/heads/main\n";
    std::ofstream(".uwu/.uwuignore").close();
}

// ---------------------------------------------------------------------------
// Basic file I/O
// ---------------------------------------------------------------------------

bool file_exists(const str& path) {
    return fs::is_regular_file(path);
}

str hash_file(const str& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::vector<char> buf((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    return sha256_hex(buf.data(), buf.size());
}

str read_file(const str& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Vec<str> read_lines(const str& path) {
    Vec<str> lines;
    std::ifstream in(path);
    str line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

void write_file(const str& path, const str& content) {
    std::ofstream(path) << content;
}

// ---------------------------------------------------------------------------
// HEAD
// ---------------------------------------------------------------------------

str get_head() {
    str content = read_file(".uwu/HEAD");
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r'))
        content.pop_back();
    if (content.rfind("ref: ", 0) == 0) {
        return content.substr(5);
    }
    return content;
}

void set_head(const str& ref) {
    write_file(".uwu/HEAD", "ref: " + ref + "\n");
}

// ---------------------------------------------------------------------------
// Staging index (`.uwu/index`, lines of `path\thash`)
// ---------------------------------------------------------------------------

Vec<IndexEntry> read_index() {
    Vec<IndexEntry> out;
    for (const auto& line : read_lines(".uwu/index")) {
        const size_t tab = line.find('\t');
        if (tab == str::npos) continue; // malformed line: skip
        out.push_back({line.substr(0, tab), line.substr(tab + 1)});
    }
    return out;
}

void write_index(const Vec<IndexEntry>& entries) {
    std::ofstream out(".uwu/index");
    for (const auto& e : entries)
        out << e.path << '\t' << e.hash << '\n';
}

// ---------------------------------------------------------------------------
// Branch refs
// ---------------------------------------------------------------------------

str resolve_ref(const str& ref) {
    if (!fs::is_regular_file(".uwu/" + ref)) return {};
    str sha = read_file(".uwu/" + ref);
    while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r'))
        sha.pop_back();
    return sha;
}

void update_ref(const str& ref, const str& sha) {
    write_file(".uwu/" + ref, sha + "\n");
}

Vec<str> list_branches() {
    Vec<str> out;
    const fs::path dir = ".uwu/refs/heads";
    if (!fs::is_directory(dir)) return out;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.is_regular_file())
            out.push_back(entry.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

str current_branch() {
    const str head = get_head();
    constexpr str_view prefix = "refs/heads/";
    if (head.rfind(prefix, 0) == 0)
        return head.substr(prefix.size());
    return {}; // detached
}

str head_sha() {
    const str head = get_head();
    if (head.rfind("refs/", 0) == 0)
        return resolve_ref(head);
    return head;
}

// ---------------------------------------------------------------------------
// Commit storage
// ---------------------------------------------------------------------------

str commit_dir(const str& sha) {
    return ".uwu/commits/" + sha;
}

bool commit_exists(const str& sha) {
    return fs::is_directory(commit_dir(sha));
}

json::Value read_commit_meta(const str& sha) {
    return json::parse(read_file(commit_dir(sha) + "/metadata.json"));
}

json::Value read_commit_tree(const str& sha) {
    return json::parse(read_file(commit_dir(sha) + "/merkle-tree.json"));
}

void write_commit_meta(const str& sha, const json::Value& meta) {
    fs::create_directories(commit_dir(sha));
    write_file(commit_dir(sha) + "/metadata.json", json::stringify(meta) + "\n");
}

void write_commit_tree(const str& sha, const json::Value& tree) {
    fs::create_directories(commit_dir(sha));
    write_file(commit_dir(sha) + "/merkle-tree.json", json::stringify(tree) + "\n");
}

Vec<str> commit_parents(const str& sha) {
    Vec<str> out;
    const json::Value meta = read_commit_meta(sha);
    const json::Value* parents = meta.get("parent_sha");
    if (parents && parents->is_array())
        for (const auto& p : parents->array)
            if (p.is_string()) out.push_back(p.string);
    return out;
}

bool commit_is_snapshot(const str& sha) {
    const json::Value meta = read_commit_meta(sha);
    const json::Value* snap = meta.get("snapshot");
    return snap && snap->is_bool() && snap->boolean;
}

str commit_merkle_root(const str& sha) {
    const json::Value meta = read_commit_meta(sha);
    const json::Value* root = meta.get("merkle_root");
    return (root && root->is_string()) ? root->string : str{};
}

// ---------------------------------------------------------------------------
// Blob storage
// ---------------------------------------------------------------------------

void store_blob(const str& sha, const str& path, const std::vector<char>& data) {
    const str blob_path = commit_dir(sha) + "/blobs/" + path;
    fs::create_directories(fs::path(blob_path).parent_path());
    const auto compressed = compress(data.data(), data.size());
    std::ofstream(blob_path, std::ios::binary)
        .write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
}

std::vector<char> load_blob(const str& sha, const str& path) {
    const str blob_path = commit_dir(sha) + "/blobs/" + path;
    std::ifstream in(blob_path, std::ios::binary);
    if (!in) return {};
    std::vector<char> compressed((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    const str data = decompress(compressed.data(), compressed.size());
    return {data.begin(), data.end()};
}

bool blob_exists(const str& sha, const str& path) {
    return fs::is_regular_file(commit_dir(sha) + "/blobs/" + path);
}

Vec<str> list_blobs(const str& sha) {
    Vec<str> out;
    const fs::path root = fs::path(commit_dir(sha)) / "blobs";
    if (!fs::is_directory(root)) return out;
    const fs::path base = root.parent_path(); // .../commits/<sha>
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            str rel = entry.path().lexically_relative(base).generic_string();
            constexpr str_view prefix = "blobs/";
            if (rel.rfind(prefix, 0) == 0) rel = rel.substr(prefix.size());
            out.push_back(rel);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// Working-directory walk
// ---------------------------------------------------------------------------

Vec<str> list_working_files(const IgnoreMatcher& ignore) {
    Vec<str> out;

    std::function<void(const fs::path&)> walk = [&](const fs::path& dir) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            const str rel = entry.path().lexically_relative(".").generic_string();

            if (entry.is_directory()) {
                if (rel == REPO_DIR) continue;       // never recurse into .uwu
                if (ignore.ignores_dir(rel)) continue; // prune ignored subtree
                walk(entry.path());
            } else if (entry.is_regular_file()) {
                if (ignore.ignores(rel)) continue;
                out.push_back(rel);
            }
        }
    };

    walk(".");
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace uwu
