#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

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

int main() {
    std::string text;
    std::cout << "Enter text to compress: ";
    std::getline(std::cin, text);

    if (text.empty()) {
        std::cout << "No input provided. Exiting.\n";
        return 0;
    }

    std::unordered_map<char, int> freq;
    for (char c : text) {
        freq[c]++;
    }

    std::priority_queue<Node*, std::vector<Node*>, Compare> pq;
    for (auto& kv : freq) {
        pq.push(new Node(kv.first, kv.second));
    }

    if (pq.size() == 1) {
        // Edge case: only one unique char
        Node* only = pq.top();
        pq.pop();
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

    std::cout << "\nHuffman Codes:\n";
    for (auto& kv : codes) {
        std::cout << kv.first << " : " << kv.second << "\n";
    }

    std::string encoded;
    for (char c : text) {
        encoded += codes[c];
    }
    std::cout << "\nEncoded bitstring: " << encoded << "\n";

    // Decode
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

    std::cout << "Decoded text: " << decoded << "\n";

    freeTree(root);
    return 0;
}
