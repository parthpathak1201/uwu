# uwu

**A minimal version control system written in C++23 — from scratch, zero external dependencies.**

uwu is a git-inspired VCS that tracks file snapshots as a directed acyclic graph (DAG) of
commits. Every hash, tree, patch, and regex engine is implemented by hand — there is no
libgit2, no OpenSSL, no zlib. Just the C++ standard library.

```
$ uwu init
initialized uwu repository
```

## Highlights

- **8 commands**, git-style CLI: `init`, `add`, `status`, `commit`, `branch`, `checkout`,
  `merge`, `rm`
- **Merkle trees** for fast change detection — a commit's file set is summarized by a single root hash
- **Delta storage** — commits store only changed files; a full snapshot is written every
  10th commit so history stays cheap to walk back
- **Custom compression** — a hand-written LZ77 + canonical Huffman codec stores blobs
- **Own SHA-256** implementation for content hashing and commit IDs
- **Own regex engine** (Thompson NFA) powering `.uwuignore`
- **Own JSON parser/serializer** with deterministic key ordering (so commit hashes are stable)
- **Safe merges** — fast-forward and non-conflicting three-way merges; conflicting merges
  abort cleanly without touching your working tree
- **Verified against git** — a differential test suite runs the same workflows in both
  tools and asserts byte-identical results

## Building

Requires **CMake ≥ 4.3** (the project needs it for its C++23 toolchain defaults) and a C++23
compiler (GCC 12+, Clang 16+, or Apple Clang). On distros that ship older CMake, you can
lower the requirement like the Dockerfile does (`sed -i 's/VERSION 4.3/VERSION 3.20/g' CMakeLists.txt`).

```bash
cmake -B build
cmake --build build -j
```

The `uwu` binary lands in `build/uwu`.

> **macOS + Homebrew GCC note:** CMake 4.x injects `-fmodules-ts` for C++23, which newer
> macOS SDKs reject (`rsize_t` error in `_string.h`). `CMakeLists.txt` already disables
> module scanning (`CMAKE_CXX_SCAN_FOR_MODULES OFF`), so no action needed. Apple Clang
> (`clang++`) is not affected either way.

## Install (use it like git)

To make `uwu` available anywhere in your terminal, install it to a directory on your
`PATH`:

```bash
# any prefix on PATH, e.g. ~/.local/bin or /usr/local/bin
cmake --install build --prefix ~/.local

# or just copy the binary
cp build/uwu ~/.local/bin/uwu
```

Then `uwu init`, `uwu commit`, … work from any directory.

## Quick start

```bash
uwu init                          # create a repository
echo "hello" > file.txt
uwu add file.txt                  # stage a file
uwu commit "my first commit"      # commit with a message
uwu status                        # show staged / modified / deleted / untracked
uwu branch dev                    # create a branch at current HEAD
uwu checkout dev                  # switch to it
uwu checkout -b feature           # or create + switch in one step
uwu checkout <commit-sha>         # detached checkout of any historical commit
uwu merge dev                     # fast-forward or three-way merge
uwu rm file.txt                   # remove a file and stage its deletion
```

## Commands

| Command | Description |
| --- | --- |
| `uwu init` | Scaffold `.uwu/` in the current directory. |
| `uwu add <path>...` | Stage files or directories for the next commit. Paths matched by `.uwuignore` are skipped with a warning. |
| `uwu status` | Report staged, modified, deleted, and untracked files versus the last commit. |
| `uwu commit <message>` | Create a commit from the staged files. Errors if nothing is staged. |
| `uwu branch <name>` | Create a new branch pointing at the current HEAD. |
| `uwu checkout <branch\|sha>` | Switch branches or check out a historical commit. |
| `uwu checkout -b <name>` | Create a new branch at the current HEAD and switch to it. |
| `uwu merge <branch>` | Fast-forward if possible, otherwise do a non-conflicting three-way merge. Conflicting merges abort safely. |
| `uwu rm <path>...` | Delete files and stage their deletion (recorded as tombstones). |

### Ignoring files

Patterns live in `.uwu/.uwuignore` (one regex per line, `#` comments allowed):

```
\.log$
build/
tmp/
```

Semantics: a file is ignored if any pattern *occurs anywhere* in its relative path
(substring match), so `\.log$` matches suffixes and `build/` matches directories. Matched
directories are pruned entirely. `!` negation is not supported (v1 scope).

## Repository layout

```
.uwu/
  ├── config                 # author + timestamp
  ├── HEAD                   # ref: refs/heads/main  (or a bare SHA when detached)
  ├── index                  # staging area: "path<TAB>hash" per line
  ├── .uwuignore             # ignore patterns
  ├── refs/heads/<branch>    # branch pointers -> commit SHA
  └── commits/<sha>/
      ├── metadata.json      # author, timestamp, message, parents, merkle root, snapshot flag
      ├── merkle-tree.json   # full file tree for this commit
      └── blobs/             # compressed file contents (only changed files, or full set on snapshots)
```

## How it works

### Commit IDs

A commit's SHA-256 is the hash of its serialized metadata. The JSON writer emits object
keys in sorted order, so identical commits always hash identically, on any platform.

### Merkle trees

Each commit stores a merkle tree over its files (path → SHA-256 of content). The root is
a single hash combining all files, built bottom-up by pairing and hashing
`SHA256(left || right)`. Comparing two commits is then an O(n) hash comparison across the leaf map instead of an
O(n) full diff of file contents — fast when most files are unchanged.

Deleted files are recorded as leaves with an all-zero **tombstone** hash, so deletions
are part of the tree and can be reconstructed precisely.

### Delta storage

Most commits change few files. uwu stores blobs **only for files whose hash changed**
since the parent commit; every 10th commit is a full snapshot, bounding how far
reconstruction must walk back. Checking out a commit walks to the nearest snapshot, then
overlays the changed files forward.

### Compression

Blob contents are compressed with a from-scratch **LZ77 + canonical Huffman** codec
(`[4-byte uncompressed size][10-bit length count][4-bit code lengths][Huffman stream]`).

### The small pieces

| Component | Purpose |
| --- | --- |
| `src/sha256.cpp` | FIPS 180-4 SHA-256, byte-order neutral, no deps. |
| `src/json.cpp` | JSON parse/stringify; `std::map` keys → deterministic output. |
| `src/regex.cpp` | Thompson-NFA regex engine (`.*+?^$|[]`, classes, escapes). |
| `src/compress.cpp` | LZ77 + canonical Huffman codec. |
| `src/merkle.cpp` | Merkle tree build, root/leaf extraction, change detection. |
| `src/ignore.cpp` | `.uwuignore` matcher on top of the regex engine. |
| `src/repo.cpp` | `.uwu/` plumbing: refs, index, commit storage, blob store, file walking. |
| `src/commands.cpp` | The 8 CLI commands + dispatch table. |
| `src/main.cpp` | Argument parsing and command dispatch. |

## Testing

The project ships four layers of tests.

```bash
# 1. C++ unit tests (regex, compress, sha256, json, merkle, ignore, repo)
ctest --test-dir build --output-on-failure

# 2. Everything in one go (unit + differential + self-contained + benchmarks)
bash test/run_all_tests.sh

# 3. Differential oracle: same workflows in git and uwu, compare results byte-for-byte
python3 test/differential_test.py

# 4. Self-contained: build 26 commits, randomly check out every one, verify byte-exact trees
python3 test/self_contained_test.py
```

The differential suite covers ignore semantics, 30-commit delta chains with detached
checkouts, fast-forward merges, non-conflicting three-way merges, tombstones, and conflict
aborts — asserting `uwu status` output and working-tree bytes match git's.

### Docker

A Dockerfile (Ubuntu 22.04 + GCC 12) builds the project and runs every test suite in one
pass:

```bash
./run_docker_tests.sh
```

See `BENCHMARKING.md` for details on the included performance benchmarks.

## Design decisions

- **Deltas, not snapshots** — most commits change few files; deltas compress better and
  write less. Full snapshots every 10 commits keep reconstruction fast.
- **Merkle trees per commit** — O(n) hash comparison beats diffing all files.
- **Backward pointers** — commits are immutable; parents are stored, never children.
- **Detached HEAD can't commit** — forces explicit branching, avoids orphan commits.
- **Checkout preserves untracked files** — work not in the VCS is never destroyed.
- **Conflicting merges abort** — conflict resolution is out of scope; uwu refuses
  silently-corrupt merges and leaves your tree untouched.

## Known limitations

- No `!` negation in `.uwuignore`, no git-ignore glob syntax (`**`, directory-only
  patterns) — regex substring matching instead.
- Symlinks are treated as regular files; file permissions are not tracked; empty
  directories are not tracked.
- No conflict resolution (merges with the same file edited differently abort).
- Single-threaded; concurrent operations are not guarded.
- No remote / push / pull.

## License

[MIT](LICENSE) — © 2026 Parth Pathak
