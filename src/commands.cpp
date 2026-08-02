#include "commands.hpp"
#include "ignore.hpp"
#include "merkle.hpp"
#include "repo.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>

std::unordered_map<type, std::function<void(const Vec<str>&)>> cmd_map;

static bool ensure_repo() {
    if (!uwu::repo_exists()) {
        std::cerr << "error: not a uwu repository (run 'uwu init' first)\n";
        return false;
    }
    return true;
}

void INIT(const Vec<str>&) {
    if (uwu::repo_exists()) {
        std::cerr << "error: uwu repository already exists\n";
        return;
    }
    uwu::init_repo();
    std::cout << "initialized uwu repository\n";
}

static void print_section(const char* title, const Vec<str>& items) {
    if (items.empty()) return;
    std::cout << title << "\n";
    for (const auto& p : items) std::cout << "  " << p << "\n";
    std::cout << "\n";
}

void STATUS(const Vec<str>&) {
    if (!ensure_repo()) return;

    const uwu::IgnoreMatcher ignore = uwu::IgnoreMatcher::from_repo();
    const Vec<str> working = uwu::list_working_files(ignore);

    // Hash map of the last commit's tree (path -> hash).
    std::map<str, str> head;
    if (const str head_sha = uwu::head_sha(); !head_sha.empty() && uwu::commit_exists(head_sha)) {
        for (const auto&[path, hash] : uwu::tree_leaves(uwu::read_commit_tree(head_sha)))
            head[path] = hash;
    }

    // Staging index (path -> hash).
    std::map<str, str> index;
    for (const auto& e : uwu::read_index()) index[e.path] = e.hash;

    // Current working tree hashes.
    std::map<str, str> disk;
    for (const auto& f : working) disk[f] = uwu::hash_file(f);

    Vec<str> staged, modified, deleted, untracked;

    // Staged: in the index, differing from the last commit (or brand new).
    for (const auto& [path, hash] : index) {
        auto it = head.find(path);
        if (it == head.end() || it->second != hash)
            staged.push_back(path);
    }

    // Modified / deleted: tracked in HEAD but changed or missing on disk.
    // Tombstone leaves mean the file is already deleted in HEAD — skip.
    for (const auto& [path, h] : head) {
        if (uwu::is_tombstone(h)) continue;
        auto di = disk.find(path);
        if (di == disk.end()) {
            if (index.find(path) == index.end()) deleted.push_back(path);
        } else if (di->second != h && index.find(path) == index.end()) {
            modified.push_back(path);
        }
    }

    // Untracked: on disk, not tracked (tombstones count as absent), not staged.
    for (const auto& [path, _] : disk) {
        auto it = head.find(path);
        const bool in_head = it != head.end() && !uwu::is_tombstone(it->second);
        if (!in_head && index.find(path) == index.end())
            untracked.push_back(path);
    }

    std::sort(staged.begin(), staged.end());
    std::sort(modified.begin(), modified.end());
    std::sort(deleted.begin(), deleted.end());
    std::sort(untracked.begin(), untracked.end());

    print_section("changes to be committed:", staged);
    print_section("modified:", modified);
    print_section("deleted:", deleted);
    print_section("untracked:", untracked);

    if (staged.empty() && modified.empty() && deleted.empty() && untracked.empty())
        std::cout << "nothing to commit, working tree clean\n";
}

// Ordinal of the commit about to be created (1 for a root commit), following
// the first-parent chain from `parent`.
static size_t next_commit_ordinal(const str& parent) {
    size_t count = 1;
    str cur = parent;
    while (!cur.empty()) {
        count++;
        const auto parents = uwu::commit_parents(cur);
        if (parents.empty()) break;
        cur = parents[0];
    }
    return count;
}

// Creates and stores a commit whose full tree is `full_tree` (path -> hash,
// tombstones allowed), with the given parents. Updates the current branch
// ref, clears the staging index, and prints the result. Returns the SHA.
static str create_commit(const std::map<str, str>& full_tree,
                         const Vec<str>& parents, const str& message) {
    const str first_parent = parents.empty() ? str{} : parents[0];
    const size_t ordinal = next_commit_ordinal(first_parent);
    const bool snapshot = (ordinal % 10 == 0);

    // Blobs: full snapshot every 10th commit, else only files whose hash
    // changed since the first parent. Deleted files (tombs) store no blob.
    std::map<str, str> old_hash;
    if (!snapshot && !first_parent.empty())
        for (const auto& leaf : uwu::tree_leaves(uwu::read_commit_tree(first_parent)))
            old_hash[leaf.path] = leaf.hash;

    Vec<uwu::FileLeaf> store;
    for (const auto& [path, h] : full_tree) {
        if (uwu::is_tombstone(h)) continue;
        if (snapshot || old_hash.count(path) == 0 || old_hash.at(path) != h)
            store.push_back({path, h});
    }

    Vec<uwu::FileLeaf> leaves;
    leaves.reserve(full_tree.size());
    for (const auto& [path, h] : full_tree) leaves.push_back({path, h});
    const uwu::json::Value tree = uwu::build_merkle_tree(leaves);

    // Author + timestamp from config.
    str author = "unknown";
    str timestamp = "0";
    for (const auto& line : uwu::read_lines(".uwu/config")) {
        if (line.rfind("author=", 0) == 0) author = line.substr(7);
        else if (line.rfind("timestamp=", 0) == 0) timestamp = line.substr(10);
    }

    // Commit SHA = hash of the serialized metadata (deterministic: json
    // writer sorts keys).
    uwu::json::Value pv = uwu::json::Value::make_array();
    for (const auto& p : parents) pv.array.push_back(uwu::json::Value::make_string(p));
    uwu::json::Value meta = uwu::json::Value::make_object();
    meta.object["author"] = uwu::json::Value::make_string(author);
    meta.object["timestamp"] = uwu::json::Value::make_string(timestamp);
    meta.object["message"] = uwu::json::Value::make_string(message);
    meta.object["parent_sha"] = pv;
    meta.object["snapshot"] = uwu::json::Value::make_bool(snapshot);
    meta.object["merkle_root"] = uwu::json::Value::make_string(uwu::tree_root(tree));
    const str sha = uwu::sha256_hex(uwu::json::stringify(meta));

    uwu::write_commit_meta(sha, meta);
    uwu::write_commit_tree(sha, tree);
    for (const auto& leaf : store) {
        const str data = uwu::read_file(leaf.path);
        uwu::store_blob(sha, leaf.path, std::vector<char>(data.begin(), data.end()));
    }

    const str ref = uwu::get_head();
    if (ref.rfind("refs/heads/", 0) != 0) {
        std::cerr << "error: cannot commit on a detached HEAD; check out a branch first\n";
        return {};
    }
    uwu::update_ref(ref, sha);
    uwu::write_index({}); // clear the staging index
    std::cout << "committed " << sha << " (" << store.size() << " blob(s))\n";
    return sha;
}

void COMMIT(const Vec<str>& tokens) {
    if (!ensure_repo()) return;

    const auto index = uwu::read_index();
    if (index.empty()) {
        std::cerr << "error: nothing staged (use 'uwu add' first)\n";
        return;
    }

    const str parent = uwu::head_sha();

    // The staging index holds only *changes* vs HEAD, so the new tree is the
    // parent's tree overlaid with staged entries (later adds override).
    std::map<str, str> merged;
    if (!parent.empty())
        for (const auto& leaf : uwu::tree_leaves(uwu::read_commit_tree(parent)))
            merged[leaf.path] = leaf.hash;
    for (const auto& e : index) merged[e.path] = e.hash;

    create_commit(merged, parent.empty() ? Vec<str>{} : Vec<str>{parent},
                  tokens.size() > 1 ? tokens[1] : "no message");
}

// Stages one path (file or directory) into the `staged` map.
// Returns the number of files newly staged.
static int stage_path(const str& raw, const uwu::IgnoreMatcher& ignore,
                      const Vec<str>& working, std::map<str, str>& staged) {
    str p = raw;
    while (!p.empty() && p.back() == '/') p.pop_back(); // tolerate trailing slash
    if (p.empty()) return 0;

    if (uwu::file_exists(p)) {
        if (ignore.ignores(p)) {
            std::cerr << "warning: '" << p << "' is ignored (see .uwuignore)\n";
            return 0;
        }
        staged[p] = uwu::hash_file(p);
        return 1;
    }

    if (std::filesystem::is_directory(p)) {
        const str prefix = p + "/";
        int count = 0;
        for (const auto& f : working) {
            if (f.rfind(prefix, 0) == 0) {
                staged[f] = uwu::hash_file(f);
                count++;
            }
        }
        if (count == 0)
            std::cerr << "error: '" << p << "' has no addable files\n";
        return count;
    }

    std::cerr << "error: '" << p << "' does not exist\n";
    return 0;
}

void ADD(const Vec<str>& tokens) {
    if (!ensure_repo()) return;

    const uwu::IgnoreMatcher ignore = uwu::IgnoreMatcher::from_repo();
    const Vec<str> working = uwu::list_working_files(ignore); // non-ignored files only

    std::map<str, str> staged; // path -> content hash (upsert)
    for (const auto& e : uwu::read_index())
        staged[e.path] = e.hash;

    int added = 0;
    for (size_t i = 1; i < tokens.size(); i++)
        added += stage_path(tokens[i], ignore, working, staged);

    Vec<uwu::IndexEntry> out;
    out.reserve(staged.size());
    for (const auto& [path, hash] : staged) out.push_back({path, hash});
    uwu::write_index(out);

    std::cout << "staged " << added << " file(s)\n";
}

// Leaf map (path -> hash, tombstones included) of a commit's tree.
// True if the working tree exactly matches HEAD (index empty + all tracked
// files byte-identical). Used to guard checkout against data loss.
static bool working_tree_clean(str* reason) {
    if (!uwu::read_index().empty()) {
        if (reason) *reason = "you have staged changes";
        return false;
    }
    const str head = uwu::head_sha();
    if (!head.empty() && uwu::commit_exists(head)) {
        for (const auto& leaf : uwu::tree_leaves(uwu::read_commit_tree(head))) {
            if (uwu::is_tombstone(leaf.hash)) continue;
            if (uwu::hash_file(leaf.path) != leaf.hash) {
                if (reason) *reason = "uncommitted changes to '" + leaf.path + "'";
                return false;
            }
        }
    }
    return true;
}

// Reconstructs the full file set of a commit: walks back to the nearest
// snapshot (or root), then overlays delta blobs forward to `sha`.
// Returns path -> file content; deleted files are absent.
static std::map<str, str> reconstruct_files(const str& sha) {
    str base = sha;
    while (!uwu::commit_is_snapshot(base) && !uwu::commit_parents(base).empty())
        base = uwu::commit_parents(base)[0];

    Vec<str> chain;
    for (str c = sha;; c = uwu::commit_parents(c)[0]) {
        chain.push_back(c);
        if (c == base) break;
    }
    std::reverse(chain.begin(), chain.end());

    // path -> (hash, owning commit). A file is re-pointed at a commit only
    // when its hash actually changed there (that's where its blob lives;
    // unchanged carry-overs keep the older commit, which has the blob).
    std::map<str, std::pair<str, str>> src;
    for (const auto& cs : chain) {
        for (const auto& leaf : uwu::tree_leaves(uwu::read_commit_tree(cs))) {
            if (uwu::is_tombstone(leaf.hash)) {
                src.erase(leaf.path);
            } else {
                auto it = src.find(leaf.path);
                if (it == src.end() || it->second.first != leaf.hash)
                    src[leaf.path] = {leaf.hash, cs};
            }
        }
    }

    std::map<str, str> out;
    for (const auto& [path, hv] : src) {
        const auto data = uwu::load_blob(hv.second, path);
        out[path] = str(data.begin(), data.end());
    }
    return out;
}


static std::map<str, str> tree_leaf_map(const str& sha) {
    std::map<str, str> m;
    for (const auto& leaf : uwu::tree_leaves(uwu::read_commit_tree(sha)))
        m[leaf.path] = leaf.hash;
    return m;
}

void BRANCH(const Vec<str>& tokens) {
    if (!ensure_repo()) return;
    if (tokens.size() < 2) {
        std::cerr << "usage: uwu branch <name>\n";
        return;
    }
    if (uwu::resolve_ref("refs/heads/" + tokens[1]) != "") {
        std::cerr << "error: branch '" << tokens[1] << "' already exists\n";
        return;
    }
    uwu::update_ref("refs/heads/" + tokens[1], uwu::head_sha());
    std::cout << "created branch '" << tokens[1] << "'\n";
}

// Removes files from disk and stages their deletion via tombstone entries
// (the next commit records them as deleted).
void RM(const Vec<str>& tokens) {
    if (!ensure_repo()) return;
    if (tokens.size() < 2) {
        std::cerr << "usage: uwu rm <path>...\n";
        return;
    }

    std::map<str, str> index;
    for (const auto& e : uwu::read_index()) index[e.path] = e.hash;
    const str head = uwu::head_sha();

    bool any = false;
    for (size_t i = 1; i < tokens.size(); i++) {
        const str path = tokens[i];
        if (!uwu::file_exists(path)) {
            std::cerr << "error: '" << path << "' does not exist\n";
            continue;
        }
        bool tracked = index.count(path) > 0;
        if (!tracked && !head.empty() && uwu::commit_exists(head)) {
            for (const auto& leaf : uwu::tree_leaves(uwu::read_commit_tree(head)))
                if (leaf.path == path) tracked = true;
        }
        if (!tracked) {
            std::cerr << "warning: '" << path << "' is not tracked, only removing from disk\n";
        }
        std::filesystem::remove(path);
        index[path] = uwu::TOMBSTONE_HASH;
        any = true;
    }

    if (!any) return;
    Vec<uwu::IndexEntry> out;
    out.reserve(index.size());
    for (const auto& [path, hash] : index) out.push_back({path, hash});
    uwu::write_index(out);
    std::cout << "removed file(s), staged for deletion\n";
}

void MERGE(const Vec<str>& tokens) {
    if (!ensure_repo()) return;
    if (tokens.size() < 2) {
        std::cerr << "usage: uwu merge <branch>\n";
        return;
    }

    bool is_branch = false;
    for (const auto& b : uwu::list_branches())
        if (b == tokens[1]) is_branch = true;
    if (!is_branch) {
        std::cerr << "error: no branch named '" << tokens[1] << "'\n";
        return;
    }

    const str head = uwu::head_sha();
    const str target = uwu::resolve_ref("refs/heads/" + tokens[1]);
    if (target == head) {
        std::cout << "already up to date\n";
        return;
    }

    str reason;
    if (!working_tree_clean(&reason)) {
        std::cerr << "error: cannot merge, " << reason << "\n";
        return;
    }
    if (uwu::get_head().rfind("refs/heads/", 0) != 0) {
        std::cerr << "error: cannot merge on a detached HEAD; check out a branch first\n";
        return;
    }

    // Merge base = first commit in target's first-parent chain that head also
    // has in its ancestry. A shared root always exists in this repo.
    std::set<str> head_anc;
    for (str c = head; !c.empty();) {
        head_anc.insert(c);
        const auto ps = uwu::commit_parents(c);
        if (ps.empty()) break;
        c = ps[0];
    }
    str base = target;
    for (;;) {
        if (head_anc.count(base)) break;
        const auto ps = uwu::commit_parents(base);
        if (ps.empty()) { base = {}; break; }
        base = ps[0];
    }
    if (base.empty()) {
        std::cerr << "error: no common ancestor with '" << tokens[1] << "'\n";
        return;
    }
    if (base == target) {
        std::cout << "already up to date\n";
        return;
    }

    // Fast-forward: HEAD is an ancestor of the target branch.
    if (base == head) {
        uwu::update_ref(uwu::get_head(), target);
        const auto files = reconstruct_files(target);
        for (const auto& [path, content] : files) {
            auto parent = std::filesystem::path(path).parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
            uwu::write_file(path, content);
        }
        for (const auto& [path, content] : reconstruct_files(head)) {
            if (files.count(path) == 0) std::filesystem::remove(path);
        }
        std::cout << "fast-forwarded to " << target << "\n";
        return;
    }

    // Three-way merge. Absence in a tree is the empty hash (real hashes are
    // always 64 hex chars). Conflict = both sides changed a path differently.
    const auto base_m = tree_leaf_map(base);
    const auto head_m = tree_leaf_map(head);
    const auto target_m = tree_leaf_map(target);

    std::map<str, str> result;
    std::set<str> keys;
    for (const auto& [p, h] : head_m) keys.insert(p);
    for (const auto& [p, h] : target_m) keys.insert(p);

    const str* conflict = nullptr;
    for (const auto& path : keys) {
        const auto hit = head_m.find(path);
        const auto tit = target_m.find(path);
        const auto bit = base_m.find(path);
        const str h = hit == head_m.end() ? str{} : hit->second;
        const str t = tit == target_m.end() ? str{} : tit->second;
        const str b = bit == base_m.end() ? str{} : bit->second;
        const bool head_changed = (h != b) || (h.empty() && !b.empty());
        const bool target_changed = (t != b) || (t.empty() && !b.empty());

        if (h == t) result[path] = t;                 // both sides agree
        else if (!target_changed) result[path] = h;   // only head changed
        else if (!head_changed) result[path] = t;     // only target changed
        else { conflict = &path; break; }             // both changed differently
    }

    if (conflict) {
        std::cerr << "error: merge conflict in '" << *conflict
                  << "'; resolve manually and commit\n";
        return;
    }

    // Materialize the merged tree, then record a two-parent commit.
    const auto head_files = reconstruct_files(head);
    const auto target_files = reconstruct_files(target);
    for (const auto& [path, hash] : result) {
        if (uwu::is_tombstone(hash)) {
            std::filesystem::remove(path);
            continue;
        }
        const bool from_head = head_m.find(path) != head_m.end() && head_m.at(path) == hash;
        const auto& content = from_head ? head_files.at(path) : target_files.at(path);
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        uwu::write_file(path, content);
    }
    for (const auto& [path, content] : head_files)
        if (result.count(path) == 0) std::filesystem::remove(path);

    create_commit(result, Vec<str>{head, target},
                  "Merge branch '" + tokens[1] + "'");
}

void CHECKOUT(const Vec<str>& tokens) {
    if (!ensure_repo()) return;
    if (tokens.size() < 2 || (tokens[1] == "-b" && tokens.size() < 3)) {
        std::cerr << "usage: uwu checkout <branch-or-commit> | -b <new-branch>\n";
        return;
    }

    // `checkout -b <name>`: create a branch at the current HEAD, then switch.
    str target = tokens[1];
    bool is_branch = false;
    if (tokens[1] == "-b") {
        target = tokens[2];
        if (uwu::resolve_ref("refs/heads/" + target) != "") {
            std::cerr << "error: branch '" << target << "' already exists\n";
            return;
        }
        uwu::update_ref("refs/heads/" + target, uwu::head_sha());
        is_branch = true;
    } else {
        for (const auto& b : uwu::list_branches()) {
            if (b == target) { is_branch = true; break; }
        }
    }

    str target_sha;
    if (is_branch) {
        target_sha = uwu::resolve_ref("refs/heads/" + target);
    } else if (uwu::commit_exists(target)) {
        target_sha = target;
    } else {
        std::cerr << "error: '" << target << "' is not a branch or commit\n";
        return;
    }

    str reason;
    if (!working_tree_clean(&reason)) {
        std::cerr << "error: cannot checkout, " << reason << "\n";
        return;
    }

    const auto files = reconstruct_files(target_sha);

    // Delete files that existed at HEAD but not in the target tree.
    const str head = uwu::head_sha();
    if (!head.empty() && uwu::commit_exists(head)) {
        for (const auto& leaf : uwu::tree_leaves(uwu::read_commit_tree(head))) {
            if (uwu::is_tombstone(leaf.hash)) continue;
            if (files.count(leaf.path) == 0)
                std::filesystem::remove(leaf.path);
        }
    }

    // Write the target's files.
    for (const auto& [path, content] : files) {
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        uwu::write_file(path, content);
    }

    if (is_branch) uwu::set_head("refs/heads/" + target);
    else uwu::set_head(target_sha);
    std::cout << "checked out " << target << " (" << files.size() << " file(s))\n";
}

type resolve_type(const Vec<str>& tokens) {
    if (tokens.empty()) return type::UNKNOWN;
    int i;
    if (tokens[0] == "init") i = 0;
    else if (tokens[0] == "status") i = 1;
    else if (tokens[0] == "commit") i = 2;
    else if (tokens[0] == "checkout") i = 3;
    else if (tokens[0] == "add") i = 4;
    else if (tokens[0] == "merge") i = 5;
    else if (tokens[0] == "branch") i = 6;
    else if (tokens[0] == "rm") i = 7;
    else return type::UNKNOWN;
    switch (i) {
        case 0: return type::INIT;
        case 1: return type::STATUS;
        case 2: return type::COMMIT;
        case 3: return type::CHECKOUT;
        case 4: return type::ADD;
        case 5: return type::MERGE;
        case 6: return type::BRANCH;
        case 7: return type::RM;
        default: return type::UNKNOWN;
    }
}

void init_cmd() {
    cmd_map[type::INIT] = INIT;
    cmd_map[type::STATUS] = STATUS;
    cmd_map[type::COMMIT] = COMMIT;
    cmd_map[type::CHECKOUT] = CHECKOUT;
    cmd_map[type::ADD] = ADD;
    cmd_map[type::MERGE] = MERGE;
    cmd_map[type::BRANCH] = BRANCH;
    cmd_map[type::RM] = RM;
}
