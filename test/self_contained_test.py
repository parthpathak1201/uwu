#!/usr/bin/env python3
import random
import shutil
import subprocess
import sys
from pathlib import Path

GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
RESET = "\033[0m"

SANDBOX = Path("/tmp/uwu_self_contained_sandbox")


def step(title):
    print(f"\n{YELLOW}=== {title} ==={RESET}")


def info(message):
    print(f"{BLUE}[INFO] {message}{RESET}")


def passed(message):
    print(f"{GREEN}[PASS] {message}{RESET}")


def fail(message):
    print(f"{RED}[FAIL] {message}{RESET}")
    sys.exit(1)


def run_uwu(*args, expect_stderr=None):
    res = subprocess.run(["uwu", *args], cwd=SANDBOX, capture_output=True, text=True)
    if expect_stderr is not None:
        if expect_stderr not in res.stderr:
            print(f"Command: uwu {' '.join(args)}")
            print(f"stdout: {res.stdout}")
            print(f"stderr: {res.stderr}")
            fail(f"Expected stderr to contain: {expect_stderr}")
        return res

    if "error:" in res.stderr:
        print(f"Command: uwu {' '.join(args)}")
        print(f"stdout: {res.stdout}")
        print(f"stderr: {res.stderr}")
        fail("uwu command reported an unexpected error")
    return res


def write(rel_path, content):
    path = SANDBOX / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def collect_tree():
    files = {}
    for path in SANDBOX.rglob("*"):
        if ".uwu" in path.parts or not path.is_file():
            continue
        files[str(path.relative_to(SANDBOX))] = path.read_bytes()
    return files


def assert_tree_equals(expected, label):
    actual = collect_tree()
    if actual != expected:
        fail(
            f"Working tree mismatch after {label}\n"
            f"  missing: {sorted(set(expected) - set(actual))}\n"
            f"  extra:   {sorted(set(actual) - set(expected))}"
        )


def assert_status_clean(label):
    res = run_uwu("status")
    if "nothing to commit, working tree clean" not in res.stdout:
        print(res.stdout)
        fail(f"Expected clean status after {label}")


def head_sha():
    head = (SANDBOX / ".uwu/HEAD").read_text().strip()
    if head.startswith("ref: "):
        return (SANDBOX / ".uwu" / head[5:]).read_text().strip()
    return head


def commit(message, paths):
    run_uwu("add", *paths)
    run_uwu("commit", message)
    sha = head_sha()
    return sha, collect_tree()


def main():
    step("Self-Contained uwu Repository Test")
    info("This test does not compare against Git. It records exact uwu snapshots, then proves uwu can return to every one.")
    shutil.rmtree(SANDBOX, ignore_errors=True)
    SANDBOX.mkdir(parents=True)
    run_uwu("init")

    step("1. Build a Commit History and Save Expected Snapshots")
    snapshots = {}
    ordered = []

    write("README.md", "# uwu self-contained test repo\n")
    write("src/app.cpp", "int main() { return 0; }\n")
    write("docs/notes.md", "version 0\n")
    write("data/table.csv", "id,value\n0,seed\n")
    sha, tree = commit("seed commit", ["README.md", "src/app.cpp", "docs/notes.md", "data/table.csv"])
    snapshots[sha] = tree
    ordered.append(sha)
    info(f"commit 00 saved: {sha[:12]} with {len(tree)} files")

    for i in range(1, 26):
        changed = []
        write("docs/notes.md", f"version {i}\nnotes line {i * i}\n")
        changed.append("docs/notes.md")

        module = f"src/module_{i % 5}.cpp"
        write(module, f"// generated in commit {i}\nint value_{i}() {{ return {i}; }}\n")
        changed.append(module)

        if i % 4 == 0:
            doc = f"docs/chapter_{i}.md"
            write(doc, f"# Chapter {i}\nThis file was born in commit {i}.\n")
            changed.append(doc)

        if i % 6 == 0:
            data = f"data/batch_{i}.csv"
            write(data, "row,total\n" + "\n".join(f"{j},{i * j}" for j in range(5)) + "\n")
            changed.append(data)

        if i == 13:
            run_uwu("rm", "data/table.csv")
            changed = [p for p in changed if p != "data/table.csv"]

        if changed:
            run_uwu("add", *changed)
        run_uwu("commit", f"self-contained change {i}")

        sha = head_sha()
        tree = collect_tree()
        snapshots[sha] = tree
        ordered.append(sha)
        info(f"commit {i:02d} saved: {sha[:12]} with {len(tree)} files")

    passed(f"Created {len(ordered)} commits and stored an expected byte snapshot for each one")

    step("2. Random Checkout Verification for Every Commit")
    shuffled = ordered[:]
    random.Random(1337).shuffle(shuffled)
    for idx, sha in enumerate(shuffled, start=1):
        run_uwu("checkout", sha)
        assert_tree_equals(snapshots[sha], f"checkout {sha[:12]}")
        assert_status_clean(f"checkout {sha[:12]}")
        info(f"{idx:02d}/{len(shuffled)} checkout matched commit {sha[:12]}")
    passed("Every randomized historical checkout reproduced the exact saved file tree")

    step("3. Branch, Checkout, Fast-Forward Merge, rm, and Conflict Abort")
    run_uwu("checkout", "main")
    latest_main = collect_tree()

    run_uwu("checkout", "-b", "feature-self")
    write("features/feature.txt", "feature branch file\n")
    branch_sha, branch_tree = commit("feature branch commit", ["features/feature.txt"])
    info(f"feature-self commit: {branch_sha[:12]}")

    run_uwu("checkout", "main")
    assert_tree_equals(latest_main, "return to main before merge")
    run_uwu("merge", "feature-self")
    assert_tree_equals(branch_tree, "fast-forward merge")
    assert_status_clean("fast-forward merge")
    passed("Branch checkout and fast-forward merge preserved the expected tree")

    run_uwu("branch", "side-self")
    write("README.md", "# uwu self-contained test repo\nmain branch edit\n")
    main_edit_sha, _ = commit("main readme edit", ["README.md"])
    info(f"main edit commit: {main_edit_sha[:12]}")

    run_uwu("checkout", "side-self")
    write("side/only.txt", "side branch non-conflicting file\n")
    side_sha, _ = commit("side branch file", ["side/only.txt"])
    info(f"side edit commit: {side_sha[:12]}")

    run_uwu("checkout", "main")
    run_uwu("merge", "side-self")
    merged = collect_tree()
    if b"main branch edit" not in merged["README.md"] or "side/only.txt" not in merged:
        fail("Non-conflicting merge did not preserve both sides")
    assert_status_clean("non-conflicting merge")
    passed("Three-way merge kept non-conflicting changes from both branches")

    run_uwu("rm", "features/feature.txt")
    run_uwu("commit", "remove feature file")
    if "features/feature.txt" in collect_tree():
        fail("rm command did not remove feature file after commit")
    assert_status_clean("rm commit")
    passed("rm staged a deletion and the deletion survived commit")

    run_uwu("branch", "conflict-self")
    write("README.md", "main conflict text\n")
    commit("main conflict edit", ["README.md"])
    run_uwu("checkout", "conflict-self")
    write("README.md", "branch conflict text\n")
    commit("branch conflict edit", ["README.md"])
    run_uwu("checkout", "main")
    before_conflict = collect_tree()
    run_uwu("merge", "conflict-self", expect_stderr="error: merge conflict in 'README.md'")
    assert_tree_equals(before_conflict, "failed conflict merge")
    passed("Conflict merge aborted without damaging the working tree")

    step("SELF-CONTAINED SUITE PASSED")
    print(f"{GREEN}uwu restored every saved commit and handled branch/merge/rm/conflict workflows correctly.{RESET}\n")


if __name__ == "__main__":
    main()
