cmake_minimum_required(VERSION 3.20)
project(uwu)

set(CMAKE_CXX_STANDARD 23)
# CMake 4.x injects -fmodules-ts for C++23, which GCC + recent macOS SDKs
# reject (rsize_t error in _string.h). Module scanning isn't needed here.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
enable_testing()

add_library(uwu_regex src/regex.cpp)
target_include_directories(uwu_regex PUBLIC include)

add_library(uwu_compress src/compress.cpp)
target_include_directories(uwu_compress PUBLIC include)

add_library(uwu_sha src/sha256.cpp)
target_include_directories(uwu_sha PUBLIC include)

add_library(uwu_json src/json.cpp)
target_include_directories(uwu_json PUBLIC include)

add_library(uwu_merkle src/merkle.cpp)
target_include_directories(uwu_merkle PUBLIC include)
target_link_libraries(uwu_merkle PUBLIC uwu_sha uwu_json)

add_library(uwu_ignore src/ignore.cpp)
target_include_directories(uwu_ignore PUBLIC include)
target_link_libraries(uwu_ignore PUBLIC uwu_regex)

add_library(uwu_core src/repo.cpp src/commands.cpp)
target_include_directories(uwu_core PUBLIC include)
target_link_libraries(uwu_core PUBLIC uwu_ignore uwu_compress uwu_sha uwu_json uwu_merkle)

add_executable(uwu src/main.cpp)
target_link_libraries(uwu uwu_core)

# Install the `uwu` binary so `cmake --install` puts it on PATH like git.
install(TARGETS uwu RUNTIME DESTINATION bin)

add_executable(test_regex test/test_regex.cpp src/regex.cpp)
target_include_directories(test_regex PRIVATE include)
add_test(NAME test_regex COMMAND test_regex)

add_executable(test_compress test/test_compress.cpp src/compress.cpp)
target_include_directories(test_compress PRIVATE include)
add_test(NAME test_compress COMMAND test_compress)

add_executable(test_sha256 test/test_sha256.cpp src/sha256.cpp)
target_include_directories(test_sha256 PRIVATE include)
add_test(NAME test_sha256 COMMAND test_sha256)

add_executable(test_json test/test_json.cpp src/json.cpp)
target_include_directories(test_json PRIVATE include)
add_test(NAME test_json COMMAND test_json)

add_executable(test_merkle test/test_merkle.cpp src/merkle.cpp src/sha256.cpp src/json.cpp)
target_include_directories(test_merkle PRIVATE include)
add_test(NAME test_merkle COMMAND test_merkle)

add_executable(test_ignore test/test_ignore.cpp src/ignore.cpp src/regex.cpp)
target_include_directories(test_ignore PRIVATE include)
add_test(NAME test_ignore COMMAND test_ignore)

add_executable(test_repo test/test_repo.cpp src/repo.cpp src/ignore.cpp src/regex.cpp src/compress.cpp src/sha256.cpp src/json.cpp)
target_include_directories(test_repo PRIVATE include)
add_test(NAME test_repo COMMAND test_repo)
