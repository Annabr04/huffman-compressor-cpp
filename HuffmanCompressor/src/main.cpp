#include <iostream>
#include <fstream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cstdint>

#include <array>

// P1: explicit leaf flag + unsigned byte symbol. No '\0' sentinel, so byte 0x00
// is an ordinary symbol.
struct Node {
    unsigned char sym;
    bool is_leaf;
    uint64_t freq;
    Node* left;
    Node* right;

    Node(unsigned char c, uint64_t f) : sym(c), is_leaf(true), freq(f), left(nullptr), right(nullptr) {}
    Node(Node* l, Node* r) : sym(0), is_leaf(false), freq(l->freq + r->freq), left(l), right(r) {}
};

struct Compare {
    bool operator()(Node* a, Node* b) {
        if (a->freq != b->freq) return a->freq > b->freq;
        // deterministic tie-break: leaves first, then by symbol
        if (a->is_leaf != b->is_leaf) return !a->is_leaf;
        return a->sym > b->sym;
    }
};

void buildCodes(Node* root, const std::string& prefix,
                std::array<std::string, 256>& codes) {
    if (!root) return;
    if (root->is_leaf) {
        // single-symbol input: root is a leaf -> 1-bit code "0"
        codes[root->sym] = prefix.empty() ? "0" : prefix;
        return;
    }
    buildCodes(root->left, prefix + "0", codes);
    buildCodes(root->right, prefix + "1", codes);
}

void freeTree(Node* root) {
    if (!root) return;
    freeTree(root->left);
    freeTree(root->right);
    delete root;
}

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
        std::cout << "Enter text to compress (or use --input file): ";
        std::getline(std::cin, inputText);
    }

    if (inputText.empty() && inputFile.empty()) {
        std::cout << "No input provided. Exiting.\n";
        return 0;
    }
    // P1: an empty input file is a valid round-trip (empty payload, rc 0).

    uint64_t originalHash = simpleHash(inputText);
    std::cout << "Original integrity hash: " << hashToHex(originalHash) << "\n";

    if (diagnosticMode) {
        std::cout << "[DIAGNOSTIC LOCK] Lossless mode enforced. Lossy transforms disabled.\n";
    }

    std::array<uint64_t, 256> freq{};
    for (unsigned char c : inputText) {
        freq[c]++;
    }

    std::priority_queue<Node*, std::vector<Node*>, Compare> pq;
    for (int s = 0; s < 256; ++s) {
        if (freq[s]) pq.push(new Node(static_cast<unsigned char>(s), freq[s]));
    }

    while (pq.size() > 1) {
        Node* a = pq.top(); pq.pop();
        Node* b = pq.top(); pq.pop();
        Node* parent = new Node(a, b);
        pq.push(parent);
    }

    Node* root = pq.empty() ? nullptr : pq.top();

    std::array<std::string, 256> codes;
    buildCodes(root, "", codes);

    std::string encoded;
    for (unsigned char c : inputText) {
        encoded += codes[c];
    }

    // Round-trip integrity check (mimics CT-01 SHA-256 style verification)
    std::string decoded;
    if (root && root->is_leaf) {
        // single-symbol tree: each bit is one symbol
        decoded.assign(encoded.size(), static_cast<char>(root->sym));
    } else {
        Node* current = root;
        for (char bit : encoded) {
            current = (bit == '0') ? current->left : current->right;
            if (current->is_leaf) {
                decoded += static_cast<char>(current->sym);
                current = root;
            }
        }
    }

    uint64_t decodedHash = simpleHash(decoded);
    bool integrityPass = (decoded == inputText) && (decodedHash == originalHash);

    std::cout << "Integrity check: " << (integrityPass ? "PASS" : "FAIL") << "\n";
    std::cout << "Decoded hash: " << hashToHex(decodedHash) << "\n";
    std::cout << "Messages checked: 1 | Passed: " << (integrityPass ? 1 : 0) << "\n";

    if (!outputFile.empty()) {
        std::ofstream out(outputFile, std::ios::binary);
        out << "DIAGNOSTIC_MODE=" << (diagnosticMode ? "true" : "false") << "\n";
        out << "ORIGINAL_HASH=" << hashToHex(originalHash) << "\n";
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
        std::ofstream d(dumpCodesFile, std::ios::binary);
        for (int s = 0; s < 256; ++s)
            if (!codes[s].empty()) d << s << " " << codes[s].size() << "\n";
    }

    freeTree(root);
    return integrityPass ? 0 : 2;
}
