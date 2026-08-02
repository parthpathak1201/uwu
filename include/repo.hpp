#pragma once

#include "common.hpp"
#include "ignore.hpp"
#include "json.hpp"

namespace uwu {

/** Name of the hidden repository directory created at `init`. */
constexpr str_view REPO_DIR = ".uwu";

/**
 * A single staging-index entry: a file path plus the SHA-256 hex of its
 * content at the time it was staged. Storing the hash lets `status` detect
 * staged-vs-working changes without re-reading whole files.
 */
struct IndexEntry {
    str path;
    str hash;
};

/**
 * Checks whether the current working directory is already a uwu repository.
 *
 * @return true if the `.uwu/` directory exists, false otherwise.
 */
bool repo_exists();

/**
 * Scaffolds a new uwu repository in the current directory.
 *
 * Creates `.uwu/`, `.uwu/refs/heads/`, `.uwu/commits/`, plus the
 * `config` (author + timestamp), `HEAD` (points to `refs/heads/main`),
 * and empty `.uwuignore` files. Caller should verify the repo doesn't
 * already exist first (see repo_exists()).
 */
void init_repo();

/** True if the file at `path` exists and is a regular file. */
bool file_exists(const str& path);

/**
 * Computes SHA-256 of a file's contents as lowercase hex.
 *
 * @param path filesystem path of the file.
 * @return 64-char hex digest, or the empty string if the file is unreadable.
 */
str hash_file(const str& path);

/**
 * Reads an entire file into a string.
 *
 * @param path filesystem path of the file to read.
 * @return the raw file contents. Empty string if the file is missing
 *         or unreadable.
 */
str read_file(const str& path);

/** Reads a file line-by-line, one string per line (no trailing newlines). */
Vec<str> read_lines(const str& path);

/** Overwrites a file with the given content, creating it if needed. */
void write_file(const str& path, const str& content);

/**
 * Resolves the current HEAD to a branch ref name or bare SHA.
 *
 * Reads `.uwu/HEAD`. If it contains `ref: <ref>`, the ref string is
 * returned (e.g. `"refs/heads/main"`); otherwise the raw value
 * (a commit SHA, indicating a detached HEAD) is returned.
 *
 * @return the current branch ref or detached commit SHA.
 */
str get_head();

/**
 * Points HEAD at the given branch ref or commit SHA.
 *
 * Writes `.uwu/HEAD` in the form `ref: <ref>`.
 *
 * @param ref branch ref (e.g. `"refs/heads/main"`) or commit SHA.
 */
void set_head(const str& ref);

/**
 * Reads the staging index (`.uwu/index`).
 *
 * Each line is `path\thash` (tab-separated).
 *
 * @return the staged entries, one per line.
 */
Vec<IndexEntry> read_index();

/**
 * Replaces the staging index (`.uwu/index`) with the given entries.
 *
 * @param entries file paths + hashes, one per line (`path\thash`).
 */
void write_index(const Vec<IndexEntry>& entries);

// ---------------------------------------------------------------------------
// Branch refs
// ---------------------------------------------------------------------------

/**
 * Resolves a branch ref to a commit SHA.
 *
 * @param ref e.g. `"refs/heads/main"`.
 * @return the commit SHA the ref points at, or "" if the ref doesn't exist.
 */
str resolve_ref(const str& ref);

/**
 * Points a branch ref at a commit SHA.
 *
 * @param ref e.g. `"refs/heads/main"`.
 * @param sha the commit SHA to point it at.
 */
void update_ref(const str& ref, const str& sha);

/**
 * Lists all branch names (files under `.uwu/refs/heads`).
 *
 * @return branch names in ascending order (empty if none).
 */
Vec<str> list_branches();

/**
 * Name of the branch HEAD currently sits on.
 *
 * @return branch name (e.g. `"main"`), or "" if HEAD is detached.
 */
str current_branch();

/**
 * Commit SHA that HEAD currently points at, resolving the branch if needed.
 *
 * @return commit SHA, or "" if the branch has no commits yet.
 */
str head_sha();

// ---------------------------------------------------------------------------
// Commit storage (`.uwu/commits/<sha>/`)
// ---------------------------------------------------------------------------

/** Full path of a commit's directory. */
str commit_dir(const str& sha);

/** True if a commit directory exists for `sha`. */
bool commit_exists(const str& sha);

/** Parsed `metadata.json` of a commit. */
json::Value read_commit_meta(const str& sha);

/** Parsed `merkle-tree.json` of a commit (see build_merkle_tree). */
json::Value read_commit_tree(const str& sha);

/** Writes a commit's `metadata.json`. */
void write_commit_meta(const str& sha, const json::Value& meta);

/** Writes a commit's `merkle-tree.json`. */
void write_commit_tree(const str& sha, const json::Value& tree);

/**
 * Parent SHAs of a commit, in stored order (first parent is the "main" one).
 *
 * @param sha commit to inspect.
 * @return parent SHAs; empty for a root commit.
 */
Vec<str> commit_parents(const str& sha);

/** True if the commit was stored as a full snapshot (every 10th commit). */
bool commit_is_snapshot(const str& sha);

/** The commit's merkle root hash (from its metadata). */
str commit_merkle_root(const str& sha);

// ---------------------------------------------------------------------------
// File blobs (compressed snapshots/deltas under `blobs/`)
// ---------------------------------------------------------------------------

/**
 * Stores a file's bytes compressed, under `commits/<sha>/blobs/<path>`.
 *
 * Mirrors the working-dir path (nested dirs created as needed). Content is
 * compressed with the uwu LZ77+Huffman codec.
 *
 * @param sha owning commit.
 * @param path forward-slash relative file path.
 * @param data raw uncompressed bytes.
 */
void store_blob(const str& sha, const str& path, const std::vector<char>& data);

/**
 * Reads and decompresses a stored blob.
 *
 * @param sha owning commit.
 * @param path forward-slash relative file path.
 * @return the uncompressed bytes (empty if the blob doesn't exist).
 */
std::vector<char> load_blob(const str& sha, const str& path);

/** True if the commit stores a blob for `path`. */
bool blob_exists(const str& sha, const str& path);

/**
 * Lists every blob path stored under `commits/<sha>/blobs/`.
 *
 * @return forward-slash relative paths in ascending order (empty if none).
 */
Vec<str> list_blobs(const str& sha);

// ---------------------------------------------------------------------------
// Working-directory traversal
// ---------------------------------------------------------------------------

/**
 * Walks the working directory and returns all non-ignored file paths.
 *
 * Excludes `.uwu/` entirely and applies the ignore matcher: ignored
 * directories are pruned (their subtrees never visited), ignored files are
 * omitted. All tracked paths are forward-slash relative to the repo root.
 *
 * @param ignore matcher compiled from `.uwuignore` patterns.
 * @return relative file paths in ascending order (empty if none).
 */
Vec<str> list_working_files(const IgnoreMatcher& ignore);

} // namespace uwu
