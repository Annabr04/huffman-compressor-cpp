#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <io.h>
static bool stdin_is_tty() { return _isatty(0) != 0; }
#else
#include <unistd.h>
static bool stdin_is_tty() { return isatty(0) != 0; }
#endif

// P2: canonical, length-limited (max 15 bits) Huffman code lengths that mirror
// CT-01's src/lib/huffman.cpp (plain_lengths / code_lengths_ws /
// canonical_codes) exactly, so per-symbol code lengths and code bits match
// ct01 and do not depend on the compiler or standard library.
//
// All working state lives in one fixed-size, caller-owned struct (an arena of
// at most 511 index-linked nodes), as in ct01's HuffWork: no heap-allocated
// tree, nothing to leak or free.
//
// This repo remains an Entropy-stage + verification reference only. It is NOT
// a second CT-01 wire format: no frame, no CRC (that is P4 / ct01's job).

namespace {

constexpr unsigned kHuffMaxBits = 15;  // ct01 kHuffMaxBits

struct HuffWork {  // subset of ct01::HuffWork, same layout semantics
    struct Node {
        std::uint64_t w;
        int l, r;
    };
    struct Item {
        std::uint64_t w;
        int id;
    };
    std::uint64_t f[256];  // flattened copy during length limiting
    Node nodes[511];
    Item heap[256];
    int leaf[256];
    unsigned depth[511];
    std::uint8_t len[256];
    std::uint16_t code[256];
    unsigned cnt[kHuffMaxBits + 1];
    unsigned next[kHuffMaxBits + 2];
};

// Unrestricted Huffman code lengths (deterministic tie-breaking by node index).
// Verbatim port of ct01 plain_lengths(): a binary min-heap over unique
// (weight, node id) keys. Leaves get ids in symbol order, merged nodes get the
// next id in creation order, so equal-weight ties never depend on
// std::priority_queue's implementation.
void plain_lengths(const std::uint64_t* f, HuffWork& w, unsigned& max_len) {
    using Node = HuffWork::Node;
    using Item = HuffWork::Item;
    auto less = [](const Item& x, const Item& y) { return x.w != y.w ? x.w < y.w : x.id < y.id; };
    Node* const nodes = w.nodes;
    Item* const heap = w.heap;
    std::size_t nn = 0, hn = 0;
    auto push = [&](Item it) {
        std::size_t i = hn++;
        heap[i] = it;
        while (i > 0) {
            const std::size_t parent = (i - 1) / 2;
            if (!less(heap[i], heap[parent])) break;
            std::swap(heap[i], heap[parent]);
            i = parent;
        }
    };
    auto pop = [&]() {
        const Item top = heap[0];
        heap[0] = heap[--hn];
        std::size_t i = 0;
        for (;;) {
            const std::size_t l = 2 * i + 1, r = l + 1;
            std::size_t m = i;
            if (l < hn && less(heap[l], heap[m])) m = l;
            if (r < hn && less(heap[r], heap[m])) m = r;
            if (m == i) break;
            std::swap(heap[i], heap[m]);
            i = m;
        }
        return top;
    };
    int* const leaf = w.leaf;
    for (int s = 0; s < 256; ++s) {
        leaf[s] = -1;
        if (f[s]) {
            leaf[s] = static_cast<int>(nn);
            nodes[nn++] = {f[s], -1, -1};
            push({f[s], leaf[s]});
        }
    }
    std::uint8_t* const len = w.len;
    std::fill(len, len + 256, std::uint8_t{0});
    max_len = 0;
    if (nn == 0) return;
    if (nn == 1) {  // single symbol: 1-bit code
        for (int s = 0; s < 256; ++s)
            if (leaf[s] >= 0) len[s] = 1;
        max_len = 1;
        return;
    }
    while (hn > 1) {
        const Item a = pop();
        const Item b = pop();
        nodes[nn++] = {a.w + b.w, a.id, b.id};
        push({a.w + b.w, static_cast<int>(nn - 1)});
    }
    unsigned* const depth = w.depth;
    std::fill(depth, depth + 511, 0u);
    for (int i = static_cast<int>(nn) - 1; i >= 0; --i) {  // parents after children
        const Node& nd = nodes[static_cast<std::size_t>(i)];
        if (nd.l >= 0) {
            depth[static_cast<std::size_t>(nd.l)] = depth[static_cast<std::size_t>(i)] + 1;
            depth[static_cast<std::size_t>(nd.r)] = depth[static_cast<std::size_t>(i)] + 1;
        }
    }
    for (int s = 0; s < 256; ++s)
        if (leaf[s] >= 0) {
            const unsigned d = depth[static_cast<std::size_t>(leaf[s])];
            len[s] = static_cast<std::uint8_t>(d);
            max_len = std::max(max_len, d);
        }
}

// Length-limited lengths of `freq` into w.len (ct01 code_lengths_ws): if the
// longest code exceeds max_bits, halve every nonzero weight with (x+1)/2 and
// retry. Converges (all ones -> balanced tree, depth <= 8). No package-merge.
void code_lengths_ws(const std::uint64_t* freq, unsigned max_bits, HuffWork& w) {
    std::copy(freq, freq + 256, w.f);
    for (;;) {
        unsigned max_len = 0;
        plain_lengths(w.f, w, max_len);
        if (max_len <= max_bits) return;
        for (auto& x : w.f)
            if (x) x = (x + 1) / 2;
    }
}

// DEFLATE-style canonical codes, assigned by (length, symbol) (ct01 canonical_codes).
void canonical_codes(const std::uint8_t* len, std::uint16_t* code, HuffWork& w) {
    unsigned* const bl_count = w.cnt;
    std::fill(bl_count, bl_count + kHuffMaxBits + 1, 0u);
    for (int s = 0; s < 256; ++s) bl_count[len[s]]++;
    bl_count[0] = 0;
    unsigned* const next = w.next;
    std::fill(next, next + kHuffMaxBits + 2, 0u);
    unsigned c = 0;
    for (unsigned bits = 1; bits <= kHuffMaxBits; ++bits) {
        c = (c + bl_count[bits - 1]) << 1;
        next[bits] = c;
    }
    for (int s = 0; s < 256; ++s)
        if (len[s]) code[s] = static_cast<std::uint16_t>(next[len[s]]++);
}

// Size ct01's Huffman stream would have: varint(n) + 128-byte length table +
// packed code bits. ct01 takes Huffman only if this is strictly smaller than
// the raw input; otherwise (>=, or empty input) it takes the stored branch.
std::uint64_t packed_size(std::uint64_t n, std::uint64_t bits) {
    std::uint64_t vlen = 1;
    for (std::uint64_t v = n; v >= 0x80; v >>= 7) ++vlen;
    return vlen + 128 + (bits + 7) / 8;
}

void append_bits(std::string& out, unsigned value, unsigned nbits) {
    for (unsigned b = nbits; b-- > 0;) out += ((value >> b) & 1u) ? '1' : '0';
}

// Canonical (puff-style) decode of `count` symbols from an ASCII '0'/'1'
// bitstring, using only the code lengths. Returns false on any malformed input.
bool canonical_decode(const std::string& bits, const std::uint8_t* len, std::uint64_t count,
                      std::string& out) {
    unsigned cnt[kHuffMaxBits + 1] = {};
    for (int s = 0; s < 256; ++s)
        if (len[s] > kHuffMaxBits) return false;
        else cnt[len[s]]++;
    cnt[0] = 0;
    unsigned offs[kHuffMaxBits + 2] = {};
    for (unsigned b = 1; b <= kHuffMaxBits; ++b) offs[b + 1] = offs[b] + cnt[b];
    unsigned char sym[256] = {};
    for (int s = 0; s < 256; ++s)
        if (len[s]) sym[offs[len[s]]++] = static_cast<unsigned char>(s);
    unsigned start[kHuffMaxBits + 1] = {};
    for (unsigned b = 2; b <= kHuffMaxBits; ++b) start[b] = start[b - 1] + cnt[b - 1];
    std::size_t pos = 0;
    out.clear();
    out.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t k = 0; k < count; ++k) {
        int code = 0, first = 0;
        bool found = false;
        for (unsigned b = 1; b <= kHuffMaxBits; ++b) {
            if (pos >= bits.size()) return false;
            const char c = bits[pos++];
            if (c != '0' && c != '1') return false;
            code |= (c == '1');
            const int n = static_cast<int>(cnt[b]);
            if (code - first < n) {
                out += static_cast<char>(sym[start[b] + static_cast<unsigned>(code - first)]);
                found = true;
                break;
            }
            first = (first + n) << 1;
            code <<= 1;
        }
        if (!found) return false;
    }
    return pos == bits.size();  // no trailing bits
}

bool stored_decode(const std::string& bits, std::string& out) {
    if (bits.size() % 8) return false;
    out.clear();
    out.reserve(bits.size() / 8);
    for (std::size_t i = 0; i < bits.size(); i += 8) {
        unsigned v = 0;
        for (std::size_t j = 0; j < 8; ++j) {
            const char c = bits[i + j];
            if (c != '0' && c != '1') return false;
            v = (v << 1) | (c == '1');
        }
        out += static_cast<char>(v);
    }
    return true;
}

}  // namespace

// Simple FNV-1a 64-bit hash for integrity (lightweight, no external deps)
uint64_t simpleHash(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hashToHex(uint64_t h) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << h;
    return ss.str();
}

int main(int argc, char* argv[]) {
    std::string inputText;
    bool diagnosticMode = false;
    std::string inputFile, outputFile, decodedFile, dumpCodesFile;

    // Simple arg parsing for device/app connection style
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--diagnostic" || arg == "-d") {
            diagnosticMode = true;
        } else if (arg == "--input" && i + 1 < argc) {
            inputFile = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "--decoded-output" && i + 1 < argc) {
            decodedFile = argv[++i];   // test hook: raw decoded bytes
        } else if (arg == "--dump-codes" && i + 1 < argc) {
            dumpCodesFile = argv[++i]; // test hook: "sym len" per used symbol
        }
    }

    if (!inputFile.empty()) {
        std::ifstream in(inputFile, std::ios::binary);
        if (!in) {
            std::cerr << "Cannot open input file: " << inputFile << "\n";
            return 1;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        inputText = buffer.str();
    } else {
        // P2: read all of stdin as bytes until EOF (no getline, so 0x0A and
        // everything after it are kept). POSIX streams have no text mode.
        // Prompt only for an interactive terminal, and on stderr, so piped
        // stdout stays clean.
        if (stdin_is_tty())
            std::cerr << "Enter data to compress, end with EOF (Ctrl-D) (or use --input file): " << std::flush;
        inputText.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }

    // Empty input (empty --input file or empty stdin) is a valid round-trip:
    // MODE=STORED, empty ENCODED, rc 0.

    uint64_t originalHash = simpleHash(inputText);
    std::cout << "Original integrity hash: " << hashToHex(originalHash) << "\n";

    if (diagnosticMode) {
        std::cout << "[DIAGNOSTIC LOCK] Lossless mode enforced. Lossy transforms disabled.\n";
    }

    std::uint64_t freq[256] = {};
    for (unsigned char c : inputText) freq[c]++;

    HuffWork w{};  // fixed-size arena; no heap nodes
    code_lengths_ws(freq, kHuffMaxBits, w);
    std::fill(w.code, w.code + 256, std::uint16_t{0});
    canonical_codes(w.len, w.code, w);

    std::uint64_t huffBits = 0;
    for (int s = 0; s < 256; ++s) huffBits += freq[s] * w.len[s];
    const std::uint64_t rawSize = inputText.size();
    const std::uint64_t packedSize = packed_size(rawSize, huffBits);
    // ct01 choose(smaller, huffman, stored): Huffman only if strictly smaller.
    const bool stored = rawSize == 0 || packedSize >= rawSize;

    std::string encoded;
    if (stored) {
        encoded.reserve(static_cast<std::size_t>(rawSize * 8));
        for (unsigned char c : inputText) append_bits(encoded, c, 8);
    } else {
        encoded.reserve(static_cast<std::size_t>(huffBits));
        for (unsigned char c : inputText) append_bits(encoded, w.code[c], w.len[c]);
    }

    // Round-trip integrity check (mimics CT-01 SHA-256 style verification):
    // decode from the bitstring + code lengths only, never from encoder state.
    std::string decoded;
    const bool decodeOk = stored ? stored_decode(encoded, decoded)
                                 : canonical_decode(encoded, w.len, rawSize, decoded);

    uint64_t decodedHash = simpleHash(decoded);
    bool integrityPass = decodeOk && (decoded == inputText) && (decodedHash == originalHash);

    std::cout << "Mode: " << (stored ? "STORED" : "HUFFMAN") << " (huffman " << packedSize
              << " bytes vs raw " << rawSize << " bytes)\n";
    std::cout << "Integrity check: " << (integrityPass ? "PASS" : "FAIL") << "\n";
    std::cout << "Decoded hash: " << hashToHex(decodedHash) << "\n";
    std::cout << "Messages checked: 1 | Passed: " << (integrityPass ? 1 : 0) << "\n";

    if (!outputFile.empty()) {
        std::ofstream out(outputFile, std::ios::binary);
        out << "DIAGNOSTIC_MODE=" << (diagnosticMode ? "true" : "false") << "\n";
        out << "ORIGINAL_HASH=" << hashToHex(originalHash) << "\n";
        out << "MODE=" << (stored ? "STORED" : "HUFFMAN") << "\n";
        out << "ENCODED=" << encoded << "\n";
        out << "INTEGRITY=" << (integrityPass ? "PASS" : "FAIL") << "\n";
        out.close();
        std::cout << "Wrote compressed payload + metadata to " << outputFile << "\n";
    } else {
        std::cout << "Encoded bitstring (truncated if long): "
                  << encoded.substr(0, 200) << (encoded.size() > 200 ? "..." : "") << "\n";
    }

    if (!integrityPass) {
        std::cout << "[FAULT] Integrity failed. In production device mode this would fall back to uncompressed.\n";
    }

    if (!decodedFile.empty()) {
        std::ofstream d(decodedFile, std::ios::binary);
        d.write(decoded.data(), static_cast<std::streamsize>(decoded.size()));
    }
    if (!dumpCodesFile.empty()) {
        // Huffman code lengths ct01 would use (dumped in STORED mode too).
        std::ofstream d(dumpCodesFile, std::ios::binary);
        for (int s = 0; s < 256; ++s)
            if (w.len[s]) d << s << " " << static_cast<unsigned>(w.len[s]) << "\n";
    }

    return integrityPass ? 0 : 2;  // nonzero rc on any round-trip mismatch
}
