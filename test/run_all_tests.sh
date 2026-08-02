#!/bin/bash
set -e

BLUE="\033[94m"
GREEN="\033[92m"
RESET="\033[0m"

echo -e "${BLUE}=== Running C++ Unit Tests (CTest, Verbose) ===${RESET}"
echo "Purpose: prove the internal building blocks work in isolation: regex, compression, SHA-256, JSON, Merkle trees, ignore rules, and repo plumbing."
ctest --test-dir build --verbose --output-on-failure

echo -e "\n${BLUE}=== Running Git vs uwu Differential Oracle Suite ===${RESET}"
echo "Purpose: run the same user workflows in Git and uwu, then compare status and working-tree bytes."
python3 test/differential_test.py

echo -e "\n${BLUE}=== Running Self-Contained uwu Snapshot/Command Suite ===${RESET}"
echo "Purpose: create a real uwu history, save every expected commit snapshot, randomly checkout every commit, and exercise branch/merge/rm/conflict behavior."
python3 test/self_contained_test.py

echo -e "\n${BLUE}=== Running Lightweight Performance Benchmarks ===${RESET}"
echo "Purpose: print rough wall-clock timings for common uwu operations so regressions are easy to spot."
python3 test/performance_benchmark.py

echo -e "\n${GREEN}=== ALL uwu TESTS PASSED ===${RESET}"
