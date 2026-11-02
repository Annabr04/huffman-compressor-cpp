# C++ Huffman Compressor (CT-01 / QuietLink compatible extensions)

Simple Huffman coding for lossless text/byte compression, extended with:
- File I/O
- Round-trip integrity verification
- Diagnostic-mode lossless lock
- Metadata output for device/app transparency

See HuffmanCompressor/README.md for usage.

> **Note (P1):** Encoded bits are not yet deterministic across compilers/standard libraries (equal-frequency tie order). Round-trip output is lossless and verified by decoded-byte SHA-256; deterministic encoding arrives with P2.
