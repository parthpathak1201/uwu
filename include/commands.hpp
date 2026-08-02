#pragma once

#include "common.hpp"

#include <functional>
#include <unordered_map>

/**
 * Discriminates which subcommand a given `uwu` invocation requested.
 * Each value maps to a handler in cmd_map.
 */
enum class type {
    INIT,     ///< `uwu init`
    STATUS,   ///< `uwu status`
    COMMIT,   ///< `uwu commit`
    CHECKOUT, ///< `uwu checkout`
    ADD,      ///< `uwu add`
    MERGE,    ///< `uwu merge`
    BRANCH,   ///< `uwu branch`
    RM,       ///< `uwu rm`
    UNKNOWN   ///< unrecognized / no command
};

namespace std {
/** Enables `type` to be used as a key in `std::unordered_map`. */
template<>
struct hash<type> {
    /** Hashes the enum by its underlying integer value. */
    size_t operator()(type t) const noexcept {
        return hash<int>{}(static_cast<int>(t));
    }
};
} // namespace std

/**
 * Registry mapping each command `type` to its handler function.
 *
 * Handlers receive the full token list (tokens[0] is the subcommand,
 * tokens[1..] are its arguments). Populated by init_cmd().
 */
extern std::unordered_map<type, std::function<void(const Vec<str>&)>> cmd_map;

/**
 * Maps the first command-line token to a `type` enum value.
 *
 * @param tokens tokenized arguments; tokens[0] must be the subcommand.
 * @return the matching type, or type::UNKNOWN if empty / unrecognized.
 */
type resolve_type(const Vec<str>& tokens);

/**
 * Populates cmd_map with all command handlers. Call once before dispatch.
 */
void init_cmd();

/** `uwu init` — scaffolds a new repository (see uwu::init_repo). */
void INIT(const Vec<str>& tokens);
/** `uwu status` — reports staged, modified, and untracked files. */
void STATUS(const Vec<str>& tokens);
/** `uwu commit` — creates a new commit from the staged files. */
void COMMIT(const Vec<str>& tokens);
/** `uwu checkout` — switches branches or restores an old commit. */
void CHECKOUT(const Vec<str>& tokens);
/** `uwu add` — stages files for the next commit. */
void ADD(const Vec<str>& tokens);
/** `uwu merge` — merges another branch into the current one. */
void MERGE(const Vec<str>& tokens);
/** `uwu branch` — creates a new branch at the current HEAD. */
void BRANCH(const Vec<str>& tokens);
/** `uwu rm` — removes files and stages their deletion. */
void RM(const Vec<str>& tokens);
