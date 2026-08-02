#!/usr/bin/env python3
import os
import sys
import json
import shutil
import subprocess
from pathlib import Path

# Setup ANSI colors for highly readable terminal reports
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
RESET = "\033[0m"

def log_info(msg):
    print(f"{BLUE}[INFO] {msg}{RESET}")

def log_success(msg):
    print(f"{GREEN}[PASS] {msg}{RESET}")

def log_fail(msg):
    print(f"{RED}[FAIL] {msg}{RESET}")

def log_step(msg):
    print(f"\n{YELLOW}=== {msg} ==={RESET}")

# Sandboxes
GIT_SANDBOX = "/tmp/git_sandbox"
UWU_SANDBOX = "/tmp/uwu_sandbox"

def clean_sandboxes():
    shutil.rmtree(GIT_SANDBOX, ignore_errors=True)
    shutil.rmtree(UWU_SANDBOX, ignore_errors=True)
    os.makedirs(GIT_SANDBOX, exist_ok=True)
    os.makedirs(UWU_SANDBOX, exist_ok=True)

# Helper to write files to both sandboxes synchronously
def write_file(rel_path, content):
    for d in (GIT_SANDBOX, UWU_SANDBOX):
        p = Path(d) / rel_path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content)

# Helper to run standard command on both sandboxes
def run_both(cmd_args, uwu_args=None):
    git_args = cmd_args
    if uwu_args is None:
        uwu_args = cmd_args
    git_res = subprocess.run(["git"] + git_args, cwd=GIT_SANDBOX, capture_output=True, text=True)
    uwu_res = subprocess.run(["uwu"] + uwu_args, cwd=UWU_SANDBOX, capture_output=True, text=True)
    if git_res.returncode != 0 or uwu_res.returncode != 0:
        log_fail("Command failed unexpectedly!")
        print(f"Git command: git {' '.join(git_args)}")
        print(f"Git exit:    {git_res.returncode}")
        print(f"Git stdout:  {git_res.stdout}")
        print(f"Git stderr:  {git_res.stderr}")
        print(f"uwu command: uwu {' '.join(uwu_args)}")
        print(f"uwu exit:    {uwu_res.returncode}")
        print(f"uwu stdout:  {uwu_res.stdout}")
        print(f"uwu stderr:  {uwu_res.stderr}")
        sys.exit(1)

# Parse git status --porcelain into standard status categories
def get_git_status():
    res = subprocess.run(["git", "status", "--porcelain", "--untracked-files=all"], cwd=GIT_SANDBOX, capture_output=True, text=True)
    staged = set()
    modified = set()
    deleted = set()
    untracked = set()
    
    for line in res.stdout.splitlines():
        if not line.strip():
            continue
        X = line[0]
        Y = line[1]
        path = line[3:].strip()
        if path.startswith('"') and path.endswith('"'):
            path = path[1:-1]
            
        if line.startswith("??"):
            untracked.add(path)
        else:
            if X in ('A', 'M', 'D'):
                staged.add(path)
            if Y == 'M':
                modified.add(path)
            if Y == 'D':
                deleted.add(path)
    return staged, modified, deleted, untracked

# Parse uwu status into standard status categories
def get_uwu_status():
    res = subprocess.run(["uwu", "status"], cwd=UWU_SANDBOX, capture_output=True, text=True)
    staged = set()
    modified = set()
    deleted = set()
    untracked = set()
    
    current_section = None
    for line in res.stdout.splitlines():
        line_strip = line.strip()
        if not line_strip:
            continue
        if line_strip == "changes to be committed:":
            current_section = staged
        elif line_strip == "modified:":
            current_section = modified
        elif line_strip == "deleted:":
            current_section = deleted
        elif line_strip == "untracked:":
            current_section = untracked
        elif line.startswith("  ") and current_section is not None:
            current_section.add(line_strip)
            
    return staged, modified, deleted, untracked

# Assert that status matches perfectly on both sandboxes
def assert_status_equal():
    g_staged, g_modified, g_deleted, g_untracked = get_git_status()
    u_staged, u_modified, u_deleted, u_untracked = get_uwu_status()
    
    if g_staged != u_staged or g_modified != u_modified or g_deleted != u_deleted or g_untracked != u_untracked:
        log_fail("Status discrepancy detected!")
        print(f"Git Staged:    {g_staged} vs uwu: {u_staged}")
        print(f"Git Modified:  {g_modified} vs uwu: {u_modified}")
        print(f"Git Deleted:   {g_deleted} vs uwu: {u_deleted}")
        print(f"Git Untracked: {g_untracked} vs uwu: {u_untracked}")
        sys.exit(1)

# Assert working directories are exactly byte-identical (metadata & content)
def assert_working_trees_equal():
    def collect_files(root):
        paths = {}
        for path in Path(root).rglob("*"):
            if ".git" in path.parts or ".uwu" in path.parts:
                continue
            rel_path = path.relative_to(root)
            paths[str(rel_path)] = path
        return paths

    git_files = collect_files(GIT_SANDBOX)
    uwu_files = collect_files(UWU_SANDBOX)
    
    git_keys = set(git_files.keys())
    uwu_keys = set(uwu_files.keys())
    
    if git_keys != uwu_keys:
        log_fail("Working directory structures differ!")
        print(f"Added in uwu: {uwu_keys - git_keys}")
        print(f"Missing in uwu: {git_keys - uwu_keys}")
        sys.exit(1)
        
    for rel_path, git_path in git_files.items():
        uwu_path = uwu_files[rel_path]
        if git_path.is_dir() != uwu_path.is_dir():
            log_fail(f"Type mismatch for {rel_path}: git is_dir={git_path.is_dir()}, uwu is_dir={uwu_path.is_dir()}")
            sys.exit(1)
        if git_path.is_file():
            if git_path.read_bytes() != uwu_path.read_bytes():
                log_fail(f"Content byte mismatch in file: {rel_path}")
                print(f"Git bytes: {git_path.read_bytes()!r}")
                print(f"uwu bytes: {uwu_path.read_bytes()!r}")
                sys.exit(1)

# ---------------------------------------------------------------------------
# Test Runner
# ---------------------------------------------------------------------------
def main():
    log_info("Starting human-readable C++23 uwu VCS vs Git Oracle differential suite")
    clean_sandboxes()
    
    # ---------------------------------------------------------
    # 1. INITIALIZATION
    # ---------------------------------------------------------
    log_step("1. Repo Initialization")
    subprocess.run(["git", "init", "-b", "main"], cwd=GIT_SANDBOX, capture_output=True)
    subprocess.run(["git", "config", "user.name", "tester"], cwd=GIT_SANDBOX)
    subprocess.run(["git", "config", "user.email", "tester@test.com"], cwd=GIT_SANDBOX)
    subprocess.run(["git", "config", "commit.gpgsign", "false"], cwd=GIT_SANDBOX)
    
    subprocess.run(["uwu", "init"], cwd=UWU_SANDBOX, capture_output=True)
    
    assert_working_trees_equal()
    assert_status_equal()
    log_success("Repos initialized and clean")

    # ---------------------------------------------------------
    # 2. BASIC ADD & COMMIT
    # ---------------------------------------------------------
    log_step("2. Basic Add & Commit")
    write_file("a.txt", "hello a\n")
    write_file("b.txt", "hello b\n")
    write_file("src/main.cpp", "int main() {}\n")
    
    assert_status_equal() # should show as untracked
    
    run_both(["add", "a.txt", "b.txt", "src"])
    assert_status_equal() # should show as staged
    
    run_both(["commit", "-m", "first"], ["commit", "first"])
    assert_status_equal() # should be clean
    assert_working_trees_equal()
    log_success("Staged files committed successfully")

    # ---------------------------------------------------------
    # 3. IGNORE SEMANTICS (.gitignore vs .uwuignore)
    # ---------------------------------------------------------
    log_step("3. Ignore Semantics (.gitignore & .uwuignore)")
    write_file(".gitignore", "debug.log\ntmp/\n")
    write_file(".uwuignore", "debug\\.log\ntmp/\n")
    # Also write directly to uwu's internal ignore file
    Path(UWU_SANDBOX).joinpath(".uwu/.uwuignore").write_text("debug\\.log\ntmp/\n")
    
    write_file("debug.log", "ignored logs\n")
    write_file("tmp/ignored.txt", "ignored text\n")
    write_file("src/non_ignored.cpp", "non-ignored cpp file\n")
    
    assert_status_equal() # ignore files should act identically
    
    run_both(["add", ".gitignore", ".uwuignore", "src/non_ignored.cpp"])
    assert_status_equal()
    
    run_both(["commit", "-m", "ignores"], ["commit", "ignores"])
    assert_status_equal()
    assert_working_trees_equal()
    log_success("Ignore rules filter out matching files identically")

    # ---------------------------------------------------------
    # 4. STRESS TEST: DELTA CHAIN TRAVERSABILITY
    # ---------------------------------------------------------
    log_step("4. Stress Test: Delta Chain & Walk-back (30 commits)")
    write_file("stress.txt", "start\n")
    run_both(["add", "stress.txt"])
    run_both(["commit", "-m", "stress start"], ["commit", "stress start"])
    
    git_shas = [subprocess.run(["git", "rev-parse", "HEAD"], cwd=GIT_SANDBOX, capture_output=True, text=True).stdout.strip()]
    uwu_shas = [Path(UWU_SANDBOX).joinpath(".uwu/refs/heads/main").read_text().strip()]
    
    for i in range(1, 31):
        write_file("stress.txt", f"start\nmodification {i}\n")
        if i % 5 == 0:
            write_file("a.txt", f"hello a version {i}\n")
            run_both(["add", "a.txt"])
        run_both(["add", "stress.txt"])
        run_both(["commit", "-m", f"edit {i}"], ["commit", f"edit {i}"])
        
        git_shas.append(subprocess.run(["git", "rev-parse", "HEAD"], cwd=GIT_SANDBOX, capture_output=True, text=True).stdout.strip())
        uwu_shas.append(Path(UWU_SANDBOX).joinpath(".uwu/refs/heads/main").read_text().strip())
        
    assert_working_trees_equal()
    log_info("Delta chain built. Stress-testing historical checkout walk-backs...")
    
    test_snapshots = [0, 5, 10, 15, 20, 25, 30]
    for idx in test_snapshots:
        g_sha = git_shas[idx]
        u_sha = uwu_shas[idx]
        log_info(f"Checking out index {idx}: git={g_sha[:8]}, uwu={u_sha[:8]}")
        
        # Detached checkout
        subprocess.run(["git", "checkout", g_sha], cwd=GIT_SANDBOX, capture_output=True)
        subprocess.run(["uwu", "checkout", u_sha], cwd=UWU_SANDBOX, capture_output=True)
        assert_working_trees_equal()
        
    # Return to main
    log_info("Re-checking out main branch")
    subprocess.run(["git", "checkout", "main"], cwd=GIT_SANDBOX, capture_output=True)
    subprocess.run(["uwu", "checkout", "main"], cwd=UWU_SANDBOX, capture_output=True)
    assert_working_trees_equal()
    log_success("VCS traversed 30 sequential deltas & snapshots with 100% byte equivalence")

    # ---------------------------------------------------------
    # 5. BRANCHING & FAST-FORWARD MERGING
    # ---------------------------------------------------------
    log_step("5. Branching & Fast-Forward Merging")
    run_both(["branch", "feat"])
    run_both(["checkout", "feat"])
    
    write_file("feat.txt", "feat file\n")
    run_both(["add", "feat.txt"])
    run_both(["commit", "-m", "feat commit"], ["commit", "feat commit"])
    
    run_both(["checkout", "main"])
    assert_working_trees_equal() # feat.txt should be absent on main
    if os.path.exists(os.path.join(UWU_SANDBOX, "feat.txt")):
        log_fail("feat.txt leaked into main branch!")
        sys.exit(1)
        
    run_both(["merge", "feat"]) # Fast-forwards main to feat
    assert_working_trees_equal()
    assert_status_equal()
    log_success("Branch switching and Fast-Forward merging match exactly")

    # ---------------------------------------------------------
    # 6. THREE-WAY MERGES
    # ---------------------------------------------------------
    log_step("6. Three-Way Merge (Non-Conflicting Divergence)")
    run_both(["branch", "side"])
    
    # Edit file A on main
    write_file("a.txt", "main edits a.txt\n")
    run_both(["add", "a.txt"])
    run_both(["commit", "-m", "main edit"], ["commit", "main edit"])
    
    # Switch to side and edit file B
    run_both(["checkout", "side"])
    write_file("b.txt", "side edits b.txt\n")
    run_both(["add", "b.txt"])
    run_both(["commit", "-m", "side edit"], ["commit", "side edit"])
    
    # Switch back to main and merge side
    run_both(["checkout", "main"])
    
    # Merge side into main. Git requires --no-edit to avoid spawning vim.
    subprocess.run(["git", "merge", "side", "--no-edit"], cwd=GIT_SANDBOX, capture_output=True)
    subprocess.run(["uwu", "merge", "side"], cwd=UWU_SANDBOX, capture_output=True)
    
    assert_working_trees_equal()
    assert_status_equal()
    
    # Assert 2 parent hashes in metadata
    uwu_merge_sha = Path(UWU_SANDBOX).joinpath(".uwu/refs/heads/main").read_text().strip()
    meta = json.loads(Path(UWU_SANDBOX).joinpath(f".uwu/commits/{uwu_merge_sha}/metadata.json").read_text())
    parents = meta.get("parent_sha", [])
    if len(parents) != 2:
        log_fail(f"Merge commit {uwu_merge_sha} doesn't have 2 parents! Found: {parents}")
        sys.exit(1)
        
    log_success("Three-way merge resolved non-conflicting changes with dual-parent tracking")

    # ---------------------------------------------------------
    # 7. DELETIONS, RECREATIONS & UNTRACKED RESTORATION
    # ---------------------------------------------------------
    log_step("7. Deletions & Recreations")
    run_both(["rm", "feat.txt"])
    assert_status_equal() # Should be staged deletion
    
    run_both(["commit", "-m", "delete feat.txt"], ["commit", "delete feat.txt"])
    assert_status_equal()
    assert_working_trees_equal()
    
    write_file("feat.txt", "recreated content\n")
    assert_status_equal() # should show as untracked again
    
    run_both(["add", "feat.txt"])
    run_both(["commit", "-m", "re-add feat.txt"], ["commit", "re-add feat.txt"])
    assert_status_equal()
    assert_working_trees_equal()
    log_success("Tracked deletions, tombstone storage, and untracked recreation verified")

    # ---------------------------------------------------------
    # 8. MERGE CONFLICT ABORTS
    # ---------------------------------------------------------
    log_step("8. Merge Conflict Handling (Safe Aborts)")
    run_both(["branch", "conflict-branch"])
    
    write_file("a.txt", "main branch's exclusive content\n")
    run_both(["add", "a.txt"])
    run_both(["commit", "-m", "main edit"], ["commit", "main edit"])
    
    run_both(["checkout", "conflict-branch"])
    write_file("a.txt", "conflict-branch's competing content\n")
    run_both(["add", "a.txt"])
    run_both(["commit", "-m", "conflict edit"], ["commit", "conflict edit"])
    
    run_both(["checkout", "main"])
    
    # uwu should abort because we don't implement conflict resolution (safe abort)
    res_uwu = subprocess.run(["uwu", "merge", "conflict-branch"], cwd=UWU_SANDBOX, capture_output=True, text=True)
    if "error: merge conflict in 'a.txt'" not in res_uwu.stderr:
        log_fail(f"uwu merge failed to abort or detect conflict! stderr: {res_uwu.stderr}")
        sys.exit(1)
        
    # Git will enter conflict state. We verify it failed then abort git merge
    subprocess.run(["git", "merge", "conflict-branch"], cwd=GIT_SANDBOX, capture_output=True)
    subprocess.run(["git", "merge", "--abort"], cwd=GIT_SANDBOX, capture_output=True)
    
    # Both should now be exactly equal to Main HEAD (undamaged state)
    assert_working_trees_equal()
    assert_status_equal()
    log_success("VCS safely aborted and preserved unmodified states on merge conflict")

    # ---------------------------------------------------------
    # FINAL SUCCESS
    # ---------------------------------------------------------
    log_step("DIFFERENTIAL ORACLE SUITE PASSED SUCCESSFULLY")
    print(f"{GREEN}All stress-tests, merges, historical delta-reconstructions, and ignores match Git 1:1.{RESET}\n")

if __name__ == "__main__":
    main()
