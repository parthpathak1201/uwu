#pragma once

#include "common.hpp"

namespace uwu {

/**
 * SHA-256 — the cryptographic hash function defined in FIPS 180-4.
 *
 * Used throughout uwu for two purposes:
 *   1. Merkle-tree leaf hashes: SHA-256 of each tracked file's bytes.
 *   2. Commit identifiers: the 64-hex-char SHA-256 of a commit's
 *      serialized contents.
 *
 * Pure standard-library implementation (no OpenSSL, no dependencies).
 * Collision-resistant for our scale; deterministic on all platforms
 * (byte-order neutral).
 *
 * Hash length is always 32 bytes / 64 lowercase hex chars.
 */

/**
 * Computes the SHA-256 digest of a raw byte buffer.
 *
 * @param data pointer to the input bytes; may be nullptr if size == 0.
 * @param size number of bytes to hash.
 * @return the 32-byte digest as 64 lowercase hex characters.
 */
str sha256_hex(const void* data, size_t size);

/**
 * Convenience overload: hashes a string's contents directly.
 *
 * @param data input string.
 * @return the 32-byte digest as 64 lowercase hex characters.
 */
str sha256_hex(const str& data);

} // namespace uwu
