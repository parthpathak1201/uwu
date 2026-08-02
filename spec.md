# uwu — Minimal Git Clone Specification

## Overview

uwu is a version control system that tracks file snapshots as a directed acyclic graph (DAG) of commits. Each commit stores file state as deltas from its parent, with merkle trees enabling fast change detection.

---

## 1. Repository Structure

```
.uwu/
  ├── config              # repo metadata (author, settings)
  ├── HEAD               # pointer to current branch/commit
  ├── refs/
  │   └── heads/         # branch pointers (main → sha, dev → sha)
  ├── commits/
  │   ├── <commit-sha>/
  │   │   ├── metadata.json      # commit info (parents, message, timestamp)
  │   │   ├── merkle-tree.json   # file hashes
  │   │   └── delta/             # patches for changed files
  │   │       ├── file1.patch
  │   │       └── file2.patch
  │   └── ...
  └── .uwuignore         # regex patterns for ignored files
```

---

## 2. Core Data Structures

### Commit Metadata

```json
{
  "sha": "hash-of-this-commit",
  "parent_sha": ["parent1-sha", "parent2-sha"],
  "branch": "main",
  "message": "commit message",
  "timestamp": 1234567890,
  "author": "user",
  "merkle_root": "root-hash"
}
```

### Merkle Tree

```json
{
  "root": "hash-combining-all-files",
  "nodes": {
    "src/main.rs": "file-hash-1",
    "README.md": "file-hash-2",
    "src/util.rs": "file-hash-3"
  }
}
```

### Delta Format

Unified diff format (standard `diff` output). Stores only lines that changed.

```
@@ -10,5 +10,6 @@
 line 9
-old line 10
+new line 10
 line 11
```

---

## 3. Operations

### init
- Create `.uwu/` directory structure
- Initialize HEAD to point to nonexistent main branch
- Create empty `.uwuignore`

### add
- Read `.uwuignore`, compile regex patterns
- Match working dir files against patterns
- Mark matched files as "staged" (tracked)
- No actual file copy yet; just metadata tracking

### status
- Compare working dir against last commit merkle tree
- Report untracked files (not in `.uwuignore`, not staged)
- Report modified tracked files (merkle hash differs)
- Report staged files

### commit
- Error if HEAD is detached; require explicit branch creation
- Build merkle tree of staged files
- Compare merkle tree vs parent commit's merkle tree
- Generate deltas only for files with changed hashes
- Compute SHA of (metadata + merkle tree + deltas)
- Write commit folder with metadata, merkle tree, and delta patches
- Update branch pointer to new commit SHA
- Clear staging area

### checkout
- Error if working dir has uncommitted changes (unless force flag)
- If switching to old commit: walk DAG backward to root, replay deltas forward
- Reconstruct full file state by applying delta patches in order
- Overwrite working dir
- Preserve untracked files
- Update HEAD pointer

### checkout -b <branch>
- Create new branch from current commit
- Update HEAD to point to new branch

### merge
- Accept target branch name
- Find common ancestor (lowest commit reachable from both branches)
- If same file modified in both branches: error, abort merge
- Otherwise: apply all deltas from ancestor → target branch to working dir
- Auto-commit with merge metadata (two parents)

---

## 4. Design Decisions

**Why deltas, not snapshots?**
Storage efficiency. Most commits change few files. Deltas compress better than full snapshots.

**Why merkle tree per commit?**
Fast change detection. Compare file hashes between commits instead of diffing all files. O(n) hash comparison beats O(n) full diff when most files unchanged.

**Why backward pointers (commit → parent)?**
Immutability. Commits never change. Forward pointers would require updating old commits on each new commit.

**Why reject detached HEAD commits?**
Simplicity. Forces explicit branching. Avoids orphan commits.

**Why preserve untracked files on checkout?**
UX. User work not in git shouldn't be destroyed. Matches real Git behavior.

**Why no merge if same file edited both branches?**
Scope constraint. Conflict resolution is complex. Binary safe approach: abort, manual resolution out of scope.

---

## 5. Compression

- File contents compressed (gzip or zstd) before storage
- Delta patches compressed
- Merkle tree and metadata stored plaintext (small enough)

---

## 6. Ignore Engine

- Regex-based pattern matching
- Patterns read from `.uwuignore`
- Same engine used for both `add` and `init` operations
- Standard regex semantics (no special git-ignore syntax like `!` negation for v1)

---

## 7. Edge Cases & Open Questions

- Symlinks: treat as files or skip?
- Binary files: diff them or treat as atomic?
- File permissions: track or ignore?
- Empty directories: track or ignore?
- Concurrent operations: assume single-threaded for now