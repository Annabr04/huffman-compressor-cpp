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

struct Node {
    char ch;
    int freq;
    Node* left;
    Node* right;

    Node(char c, int f) : ch(c), freq(f), left(nullptr), right(nullptr) {}
    Node(Node* l, Node* r) : ch('\0'), freq(l->freq + r->freq), left(l), right(r) {}
};

struct Compare {
    bool operator()(Node* a, Node* b) {
        return a->freq > b->freq;
    }
};

void buildCodes(Node* root, const std::string& prefix,
                std::unordered_map<char, std::string>& codes) {
    if (!root) return;
    if (!root->left && !root->right && root->ch != '\0') {
        codes[root->ch] = prefix;
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
    std::string inputFile, outputFile;

    // Simple arg parsing for device/app connection style
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--diagnostic" || arg == "-d") {
            diagnosticMode = true;
        } else if (arg == "--input" && i + 1 < argc) {
            inputFile = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            outputFile = argv[++i];
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

    if (inputText.empty()) {
        std::cout << "No input provided. Exiting.\n";
        return 0;
    }

    uint64_t originalHash = simpleHash(inputText);
    std::cout << "Original integrity hash: " << hashToHex(originalHash) << "\n";

    if (diagnosticMode) {
        std::cout << "[DIAGNOSTIC LOCK] Lossless mode enforced. Lossy transforms disabled.\n";
    }

    std::unordered_map<char, int> freq;
    for (unsigned char c : inputText) {
        freq[static_cast<char>(c)]++;
    }

    std::priority_queue<Node*, std::vector<Node*>, Compare> pq;
    for (auto& kv : freq) {
        pq.push(new Node(kv.first, kv.second));
    }

    if (pq.empty()) return 0;

    if (pq.size() == 1) {
        Node* only = pq.top(); pq.pop();
        Node* root = new Node(only, new Node('\0', 0));
        pq.push(root);
    }

    while (pq.size() > 1) {
        Node* a = pq.top(); pq.pop();
        Node* b = pq.top(); pq.pop();
        Node* parent = new Node(a, b);
        pq.push(parent);
    }

    Node* root = pq.top();

    std::unordered_map<char, std::string> codes;
    buildCodes(root, "", codes);

    std::string encoded;
    for (unsigned char c : inputText) {
        encoded += codes[static_cast<char>(c)];
    }

    // Round-trip integrity check (mimics CT-01 SHA-256 style verification)
    std::string decoded;
    Node* current = root;
    for (char bit : encoded) {
        if (bit == '0') current = current->left;
        else current = current->right;
        if (!current->left && !current->right) {
            decoded += current->ch;
            current = root;
        }
    }

    uint64_t decodedHash = simpleHash(decoded);
    bool integrityPass = (decoded == inputText) && (decodedHash == originalHash);

    std::cout << "Integrity check: " << (integrityPass ? "PASS" : "FAIL") << "\n";
    std::cout << "Decoded hash: " << hashToHex(decodedHash) << "\n";
    std::cout << "Messages checked: 1 | Passed: " << (integrityPass ? 1 : 0) << "\n";

    if (!outputFile.empty()) {
        std::ofstream out(outputFile);
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

    freeTree(root);
    return integrityPass ? 0 : 2;
}
