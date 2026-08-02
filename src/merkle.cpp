#include "merkle.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <map>

namespace uwu {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Builds a JSON leaf map keyed by path, given leaves already sorted by path.
static json::Value make_leaves_map(const Vec<FileLeaf>& sorted) {
    json::Value map = json::Value::make_object();
    for (const auto& leaf : sorted)
        map.object[leaf.path] = json::Value::make_string(leaf.hash);
    return map;
}

// ---------------------------------------------------------------------------
// Tree construction
// ---------------------------------------------------------------------------

json::Value build_merkle_tree(const Vec<FileLeaf>& leaves) {
    // Sort by path so identical file sets always produce the same tree.
    Vec<FileLeaf> sorted = leaves;
    std::sort(sorted.begin(), sorted.end(),
              [](const FileLeaf& a, const FileLeaf& b) { return a.path < b.path; });

    // The "root" field: hash of the tree. Empty tree -> hash of "".
    str root;
    json::Value internal = json::Value::make_object();
    if (sorted.empty()) {
        root = sha256_hex("");
    } else if (sorted.size() == 1) {
        root = sorted[0].hash;
    } else {
        // Work level by level. `cur` holds the hashes at the current level,
        // starting with the leaf hashes.
        Vec<str> cur;
        cur.reserve(sorted.size());
        for (const auto& leaf : sorted) cur.push_back(leaf.hash);

        int level = 1;
        while (cur.size() > 1) {
            Vec<str> next;
            next.reserve((cur.size() + 1) / 2);
            for (size_t i = 0; i < cur.size(); i += 2) {
                if (i + 1 < cur.size()) {
                    // parent = SHA-256(left_hash + right_hash)
                    const str h = sha256_hex(cur[i] + cur[i + 1]);
                    next.push_back(h);
                    const str key = "level-" + std::to_string(level) + "-node-" +
                                    std::to_string(i / 2);
                    internal.object[key] = json::Value::make_string(h);
                } else {
                    next.push_back(cur[i]); // odd leftover promotes
                }
            }
            cur = std::move(next);
            level++;
        }
        root = cur[0];
    }

    json::Value out = json::Value::make_object();
    out.object["root"] = json::Value::make_string(root);
    json::Value nodes = json::Value::make_object();
    nodes.object["leaves"] = make_leaves_map(sorted);
    nodes.object["internal"] = internal; // default Null if unused
    out.object["nodes"] = std::move(nodes);
    return out;
}

str tree_root(const json::Value& tree) {
    const json::Value* root = tree.get("root");
    return (root && root->is_string()) ? root->string : str{};
}

Vec<FileLeaf> tree_leaves(const json::Value& tree) {
    Vec<FileLeaf> out;
    const json::Value* nodes = tree.get("nodes");
    if (!nodes || !nodes->is_object()) return out;
    const json::Value* leaves = nodes->get("leaves");
    if (!leaves || !leaves->is_object()) return out;

    out.reserve(leaves->object.size());
    for (const auto& [path, hash] : leaves->object) {
        if (hash.is_string()) out.push_back({path, hash.string});
    }
    // std::map iterates in ascending key order already.
    return out;
}

Vec<str> changed_paths(const json::Value& a, const json::Value& b) {
    const auto& la = tree_leaves(a);
    const auto& lb = tree_leaves(b);

    Vec<str> changed;
    std::map<str, str> ha;
    for (const auto& leaf : la) ha[leaf.path] = leaf.hash;
    std::map<str, str> hb;
    for (const auto& leaf : lb) hb[leaf.path] = leaf.hash;

    // Every path present in either tree.
    Vec<str> all;
    all.reserve(ha.size() + hb.size());
    for (const auto& [path, _] : ha) all.push_back(path);
    for (const auto& [path, _] : hb)
        if (ha.find(path) == ha.end()) all.push_back(path);
    std::sort(all.begin(), all.end());

    for (const auto& path : all) {
        auto ia = ha.find(path);
        auto ib = hb.find(path);
        const str h_a = (ia != ha.end()) ? ia->second : str(TOMBSTONE_HASH);
        const str h_b = (ib != hb.end()) ? ib->second : str(TOMBSTONE_HASH);
        if (h_a != h_b) changed.push_back(path);
    }
    return changed;
}

} // namespace uwu
