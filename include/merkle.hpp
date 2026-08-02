#pragma once

#include "common.hpp"
#include "json.hpp"

namespace uwu {

/**
 * A leaf of the merkle tree: a tracked file path and its SHA-256 content
 * hash (64 lowercase hex chars).
 */
struct FileLeaf {
    str path;
    str hash;
};

/**
 * Reserved hash marking a file as *deleted* in a commit's merkle tree.
 *
 * A deleted tracked file is recorded as a leaf with this all-zero hash
 * instead of a real content hash. On checkout reconstruction the marker
 * tells us to remove the file from the working directory. No blob is
 * stored for deleted files.
 */
constexpr str_view TOMBSTONE_HASH = "0000000000000000000000000000000000000000000000000000000000000000";

/** True if `h` is the reserved tombstone hash (i.e. the file was deleted). */
inline bool is_tombstone(str_view h) {
    return h == TOMBSTONE_HASH;
}

/**
 * Builds a merkle tree from file leaves and returns it as JSON.
 *
 * Leaves are sorted by path first, so the result is independent of input
 * order (required for deterministic commit hashing). The binary tree is
 * built bottom-up:
 *   - leaves pair up left-to-right;
 *   - parent hash = SHA-256(left_hash + right_hash)  (hex concatenation);
 *   - an odd leftover hash promotes unchanged to the next level;
 *   - repeat until a single root hash remains.
 *
 * Serialized shape:
 * @code
 * {
 *   "root": "<root-hash>",
 *   "nodes": {
 *     "leaves":   { "<path>": "<hash>", ... },
 *     "internal": { "level-<n>-node-<m>": "<hash>", ... }
 *   }
 * }
 * @endcode
 * Levels are numbered from the bottom (level 1 = parents of leaves) up to
 * the root. With 0 leaves the root is SHA-256 of the empty string.
 *
 * @param leaves the tracked files (path + content hash); duplicates are
 *        overwritten (last wins) after sorting.
 * @return the serialized merkle tree.
 */
json::Value build_merkle_tree(const Vec<FileLeaf>& leaves);

/**
 * Extracts the root hash from a serialized merkle tree.
 *
 * @param tree a value produced by build_merkle_tree().
 * @return the root hash string, or an empty string if `root` is absent.
 */
str tree_root(const json::Value& tree);

/**
 * Extracts the leaf map (path -> hash) from a serialized merkle tree.
 *
 * This is the structure used for per-file change detection: two commits'
 * leaf maps are compared path-by-path.
 *
 * @param tree a value produced by build_merkle_tree().
 * @return the leaves in ascending path order (empty if none).
 */
Vec<FileLeaf> tree_leaves(const json::Value& tree);

/**
 * Compares two merkle trees and returns paths whose hash differs.
 *
 * A path is "changed" if it exists in either tree with a different hash,
 * or exists in only one. Tombstones count as a normal differing hash.
 *
 * @param a first tree (e.g. ancestor commit).
 * @param b second tree (e.g. current working state).
 * @return changed paths in ascending order.
 */
Vec<str> changed_paths(const json::Value& a, const json::Value& b);

} // namespace uwu
