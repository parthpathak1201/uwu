#!/bin/bash
set -e

# Setup ANSI colors for the host script
BLUE="\033[94m"
RESET="\033[0m"

echo -e "${BLUE}=== Building C++23 uwu VCS Docker Image ===${RESET}"
docker build -t uwu-vcs-tester .

echo -e "\n${BLUE}=== Running Entire uwu Test Suite in Container ===${RESET}"
docker run --rm uwu-vcs-tester
