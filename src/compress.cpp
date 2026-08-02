#include "compress.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <queue>
#include <stdexcept>
#include <vector>

namespace uwu {
    // --- LZ77 constants ---
    constexpr int WINDOW_SIZE = 32768;
    constexpr int MAX_MATCH = 258;
    constexpr int MIN_MATCH = 3;
    constexpr int HASH_BITS = 15;
    constexpr int HASH_SIZE = 1 << HASH_BITS;
    constexpr int MAX_CHAIN = 32;

    // --- Huffman constants ---
    constexpr int NUM_SYMBOLS = 258; // 0-255 literals + 256 match + 257 EOS
    constexpr int MAX_CODE_LEN = 15;

    namespace {
        // --- Bit writer ---
        class BitWriter {
            std::vector<char> &buf_;
            int byte_pos_ = 0;
            int bit_pos_ = 0; // 0-7, next bit to write (LSB first)
        public:
            explicit BitWriter(std::vector<char> &buf) : buf_(buf) {
                buf_.push_back(0);
            }

            void write_bit(int bit) {
                if (bit_pos_ == 8) {
                    buf_.push_back(0);
                    byte_pos_++;
                    bit_pos_ = 0;
                }
                if (bit) buf_[byte_pos_] |= (1 << bit_pos_);
                bit_pos_++;
            }

            void write_bits(int value, int nbits) {
                for (int i = nbits - 1; i >= 0; i--) {
                    write_bit((value >> i) & 1); // MSB first
                }
            }

            void flush() {
                if (bit_pos_ > 0 && bit_pos_ < 8) {
                    bit_pos_ = 0;
                    byte_pos_++;
                    buf_.push_back(0);
                }
            }
        };
    }

    namespace {
        // --- Bit reader ---
        class BitReader {
            const uint8_t *data_;
            size_t size_;
            size_t byte_pos_ = 0;
            int bit_pos_ = 0;

        public:
            BitReader(const void *data, size_t size)
                : data_(static_cast<const uint8_t *>(data)), size_(size) {
            }

            int read_bit() {
                if (byte_pos_ >= size_) return -1;
                const int bit = (data_[byte_pos_] >> bit_pos_) & 1;
                bit_pos_++;
                if (bit_pos_ == 8) {
                    byte_pos_++;
                    bit_pos_ = 0;
                }
                return bit;
            }

            int read_bits(const int nbits) {
                int value = 0;
                for (int i = 0; i < nbits; i++) {
                    const int bit = read_bit();
                    if (bit < 0) return -1;
                    value = (value << 1) | bit; // MSB first
                }
                return value;
            }

            [[nodiscard]] bool eof() const {
                return byte_pos_ >= size_ && bit_pos_ == 0;
            }
        };
    }

    namespace {
        // --- Huffman tree builder ---
        struct HuffNode {
            int symbol;
            int freq;
            int left;
            int right;
        };
    }

    static void build_code_lengths(const std::vector<int> &freqs,
                                   std::vector<int> &code_lengths) {
        code_lengths.assign(NUM_SYMBOLS, 0);

        std::vector<HuffNode> nodes;
        nodes.reserve(NUM_SYMBOLS * 2);
        auto cmp = [&](const int a, const int b) { return nodes[a].freq > nodes[b].freq; };
        std::priority_queue<int, std::vector<int>, decltype(cmp)> heap(cmp);

        for (int i = 0; i < NUM_SYMBOLS; i++) {
            if (freqs[i] > 0) {
                nodes.push_back({i, freqs[i], -1, -1});
                heap.push(static_cast<int>(nodes.size()) - 1);
            }
        }

        if (heap.empty()) return;
        if (heap.size() == 1) {
            code_lengths[nodes[heap.top()].symbol] = 1;
            return;
        }

        while (heap.size() > 1) {
            const int ai = heap.top();
            heap.pop();
            const int bi = heap.top();
            heap.pop();
            nodes.push_back({-1, nodes[ai].freq + nodes[bi].freq, ai, bi});
            heap.push(static_cast<int>(nodes.size()) - 1);
        }

        const int root = heap.top();
        if (nodes[root].left < 0 && nodes[root].right < 0) {
            code_lengths[nodes[root].symbol] = 1;
            return;
        }

        struct Frame {
            int node;
            int depth;
        };
        std::vector<Frame> stack = {{root, 0}};
        while (!stack.empty()) {
            auto [node, depth] = stack.back();
            stack.pop_back();
            if (const auto &n = nodes[node]; n.left < 0 && n.right < 0) {
                code_lengths[n.symbol] = depth;
            } else {
                if (n.left >= 0) stack.push_back({.node = n.left, .depth = depth + 1});
                if (n.right >= 0) stack.push_back({.node = n.right, .depth = depth + 1});
            }
        }
    }

    // Generate canonical codes from code lengths
    static void build_canonical_codes(const std::vector<int> &code_lengths,
                                      std::vector<int> &codes) {
        codes.assign(NUM_SYMBOLS, 0);

        // Find symbols with non-zero length, sort by (len, sym)
        std::vector<std::pair<int, int> > sym_lens; // (length, symbol)
        for (int i = 0; i < NUM_SYMBOLS; i++) {
            if (code_lengths[i] > 0)
                sym_lens.emplace_back(code_lengths[i], i);
        }
        std::ranges::sort(sym_lens);

        int code = 0;
        int prev_len = 0;
        for (auto [len, sym]: sym_lens) {
            code <<= (len - prev_len);
            codes[sym] = code;
            code++;
            prev_len = len;
        }
    }

    namespace {
        // --- LZ77 hash chain ---
        struct LZ77 {
            const uint8_t *data_;
            size_t size_;
            std::vector<int> head_;
            std::vector<int> chain_;
            size_t next_pos_ = 0;

            LZ77(const uint8_t *data, size_t size)
                : data_(data), size_(size)
                  , head_(HASH_SIZE, -1)
                  , chain_(WINDOW_SIZE, -1) {
            }

            static uint32_t hash(const uint8_t *p) {
                return (static_cast<uint32_t>(p[0]) << 4) ^ (static_cast<uint32_t>(p[1]) << 2) ^ static_cast<uint32_t>(p
                           [2]);
            }

            void insert(const size_t pos) {
                if (pos + 2 >= size_) return;
                const uint32_t h = hash(data_ + pos);
                const size_t slot = pos % WINDOW_SIZE;
                chain_[slot] = head_[h];
                head_[h] = static_cast<int>(pos);
            }

            struct Match {
                int length;
                int distance;
            };

            [[nodiscard]] Match find_match(size_t pos) const {
                Match best = {.length = 0, .distance = 0};
                if (pos + MIN_MATCH > size_) return best;

                const uint32_t h = hash(data_ + pos);
                int candidate = head_[h];
                const int limit = std::max(0, static_cast<int>(pos) - WINDOW_SIZE);
                int checks = 0;

                while (candidate >= limit && checks < MAX_CHAIN) {
                    checks++;
                    // Quick check: first 3 bytes should match
                    if (data_[candidate] == data_[pos] &&
                        data_[candidate + 1] == data_[pos + 1] &&
                        data_[candidate + 2] == data_[pos + 2]) {
                        int len = MIN_MATCH;
                        while ((size_t) (pos + len) < size_ &&
                               len < MAX_MATCH &&
                               data_[candidate + len] == data_[pos + len]) {
                            len++;
                        }
                        if (len > best.length) {
                            best.length = len;
                            best.distance = static_cast<int>(pos - candidate);
                        }
                    }
                    // Ensure we don't go out of bounds on chain_
                    const int idx = candidate % WINDOW_SIZE;
                    if (idx < 0 || idx >= static_cast<int>(chain_.size())) break;
                    candidate = chain_[idx];
                }

                return best;
            }
        };
    }

    namespace {
        // --- Compress ---
        struct Symbol {
            enum Type { LITERAL, MATCH, END } type;

            uint8_t byte; // for LITERAL
            int length; // for MATCH
            int distance; // for MATCH
        };
    }

    static std::vector<Symbol> lz77_encode(const uint8_t *data, const size_t size) {
        std::vector<Symbol> symbols;
        symbols.reserve(size);
        LZ77 lz(data, size);

        size_t pos = 0;
        while (pos < size) {
            auto [length, distance] = lz.find_match(pos);

            if (length >= MIN_MATCH) {
                symbols.push_back({.type = Symbol::MATCH, .byte = 0, .length = length, .distance = distance});
                for (int i = 0; i < length; i++) {
                    lz.insert(pos);
                    pos++;
                }
            } else {
                symbols.push_back({.type = Symbol::LITERAL, .byte = data[pos], .length = 0, .distance = 0});
                lz.insert(pos);
                pos++;
            }
        }

        return symbols;
    }

    namespace {
        struct EncodedBlock {
            std::vector<char> data;
        };
    }

    static EncodedBlock huffman_encode(const std::vector<Symbol> &symbols) {
        // Count frequencies
        std::vector<int> freqs(NUM_SYMBOLS, 0);
        for (const auto &sym: symbols) {
            if (sym.type == Symbol::LITERAL) {
                freqs[sym.byte]++;
            } else if (sym.type == Symbol::MATCH) {
                freqs[256]++; // match marker
            }
        }
        // Always add EOS marker to ensure clean termination
        freqs[257] = 1;

        // Build Huffman codes
        std::vector<int> code_lengths;
        build_code_lengths(freqs, code_lengths);
        std::vector<int> codes;
        build_canonical_codes(code_lengths, codes);

        std::vector<char> buf;
        BitWriter bw(buf);

        int num_lengths = NUM_SYMBOLS;
        while (num_lengths > 0 && code_lengths[num_lengths - 1] == 0) num_lengths--;

        bw.write_bits(num_lengths, 10);
        for (int i = 0; i < num_lengths; i++) {
            bw.write_bits(code_lengths[i], 4);
        }

        for (const auto &[type, byte, length, distance]: symbols) {
            if (type == Symbol::LITERAL) {
                const int code = codes[byte];
                const int len = code_lengths[byte];
                bw.write_bits(code, len);
            } else if (type == Symbol::MATCH) {
                // Write match marker (256)
                const int code = codes[256];
                const int len = code_lengths[256];
                bw.write_bits(code, len);
                // Write length (8 bits) and distance (15 bits) raw
                bw.write_bits(length - MIN_MATCH, 8);
                bw.write_bits(distance - 1, 15);
            }
        }

        // Encode EOS marker
        bw.write_bits(codes[257], code_lengths[257]);

        bw.flush();

        EncodedBlock block;
        block.data = std::move(buf);
        return block;
    }

    std::vector<char> compress(const void *data, size_t size) {
        if (size == 0) {
            std::vector<char> result(4, 0);
            return result;
        }

        const auto *bytes = static_cast<const uint8_t *>(data);

        // LZ77 encode
        const auto symbols = lz77_encode(bytes, size);

        // Huffman encode
        auto [DATA] = huffman_encode(symbols);

        // Build final output: [uncompressed_size:4] [huffman_bitstream...]
        std::vector<char> output;
        output.reserve(4 + DATA.size());

        // Write uncompressed size
        output.push_back(static_cast<char>(size & 0xFF));
        output.push_back(static_cast<char>((size >> 8) & 0xFF));
        output.push_back(static_cast<char>((size >> 16) & 0xFF));
        output.push_back(static_cast<char>((size >> 24) & 0xFF));

        // Append the huffman-encoded bitstream
        output.insert(output.end(), DATA.begin(), DATA.end());

        return output;
    }

    // --- Decompress ---

    namespace {
        struct HuffmanDecoder {
            struct Node {
                int symbol;
                int left;
                int right;
            };

            std::vector<Node> nodes;

            void build(const std::vector<int> &code_lengths) {
                nodes.clear();
                nodes.reserve(NUM_SYMBOLS * 2);
                nodes.push_back({.symbol = -1, .left = -1, .right = -1}); // root

                // Build canonical codes
                std::vector<int> codes;
                build_canonical_codes(code_lengths, codes);

                // Insert each symbol into the tree (MSB-first traversal)
                for (int sym = 0; sym < NUM_SYMBOLS; sym++) {
                    const int len = code_lengths[sym];
                    if (len == 0) continue;
                    const int code = codes[sym];

                    int node = 0;
                    for (int i = len - 1; i >= 0; i--) {
                        int bit = (code >> i) & 1; // MSB first
                        int *child = bit ? &nodes[node].right : &nodes[node].left;
                        if (*child == -1) {
                            *child = static_cast<int>(nodes.size());
                            nodes.push_back({-1, -1, -1});
                        }
                        node = *child;
                    }
                    nodes[node].symbol = sym;
                }
            }

            int decode(BitReader &reader) const {
                int node = 0;
                while (nodes[node].symbol == -1) {
                    const int bit = reader.read_bit();
                    if (bit < 0) return -1;
                    node = bit ? nodes[node].right : nodes[node].left;
                    if (node < 0) return -1;
                }
                return nodes[node].symbol;
            }
        };
    }

    //outputs a std::string
    std::string decompress(const void *data, size_t size) {
        if (size < 4) {
            throw std::runtime_error("invalid compressed data");
        }

        const auto bytes = static_cast<const uint8_t *>(data);

        // Read uncompressed size
        const size_t uncomp_size = static_cast<size_t>(bytes[0])
                             | (static_cast<size_t>(bytes[1]) << 8)
                             | (static_cast<size_t>(bytes[2]) << 16)
                             | (static_cast<size_t>(bytes[3]) << 24);

        if (uncomp_size == 0) return {};

        // Read from bitstream starting at offset 4
        // Read code lengths (same format as encoder wrote them)
        // Actually we wrote the bitstream starting from byte 4.
        // The bitstream begins with: num_lengths (10 bits), then code_lengths (4 bits each)
        BitReader reader(static_cast<const char *>(data) + 4, size - 4);

        const int num_lengths = reader.read_bits(10);
        if (num_lengths < 0) throw std::runtime_error("truncated header");

        std::vector<int> code_lengths(NUM_SYMBOLS, 0);
        for (int i = 0; i < num_lengths; i++) {
            const int len = reader.read_bits(4);
            if (len < 0) throw std::runtime_error("truncated code lengths");
            code_lengths[i] = len;
        }

        // Build Huffman decoder
        HuffmanDecoder decoder;
        decoder.build(code_lengths);

        // Decode symbols
        std::string output;
        output.reserve(uncomp_size);

        while (output.size() < uncomp_size) {
            int sym = decoder.decode(reader);
            if (sym < 0 || sym == 257) break;

            if (sym < 256) {
                output.push_back((char) sym);
            } else if (sym == 256) {
                // Match marker
                int length = reader.read_bits(8);
                if (length < 0) break;
                length += MIN_MATCH;

                int distance = reader.read_bits(15);
                if (distance < 0) break;
                distance += 1;

                // Copy from already-decompressed output
                const size_t src_pos = output.size() - distance;
                for (int i = 0; i < length; i++) {
                    output.push_back(output[src_pos + i]);
                }
            }
        }

        return output;
    }
}
