#pragma once

#include <string>
#include <vector>

namespace uwu {

/**
 * Compresses raw bytes using a custom LZ77 + canonical Huffman codec.
 *
 * Output format:
 *   [4-byte little-endian uncompressed size][10-bit length count]
 *   [4-bit code lengths per symbol][Huffman-coded symbol stream]
 *
 * @param data pointer to the input bytes
 * @param size number of input bytes
 * @return the compressed byte stream. Never throws on valid input.
 */
std::vector<char> compress(const void* data, size_t size);

/**
 * Decompresses a stream previously produced by compress().
 *
 * @param data pointer to the compressed stream
 * @param size number of compressed bytes
 * @return the reconstructed original bytes.
 * @throws std::runtime_error if the stream is corrupt or truncated.
 */
std::string decompress(const void* data, size_t size);

/** Convenience overload: compress a std::string in place. */
inline std::vector<char> compress(const std::string& s) {
    return compress(s.data(), s.size());
}

/** Convenience overload: decompress a byte vector back to a string. */
inline std::string decompress(const std::vector<char>& v) {
    return decompress(v.data(), v.size());
}

}
