# C++ Huffman Compressor (CT-01 / QuietLink compatible extensions)

Simple Huffman coding for lossless text/byte compression, extended with:
- File I/O
- Round-trip integrity verification
- Diagnostic-mode lossless lock
- Metadata output for device/app transparency

See HuffmanCompressor/README.md for usage.

> **P2:** Code lengths mirror CT-01's `huffman.cpp` exactly: deterministic tie-breaking by a unique (weight, node id) key, length-limited to 15 bits by (x+1)/2 frequency flattening, and DEFLATE-style canonical codes by (length, symbol). Encoded output is byte-identical across compilers (verified with zig c++ and g++). If Huffman would not be smaller than the raw input (CT-01's varint + 128-byte table + packed bits >= raw size), the payload is stored raw (`MODE=STORED`).
>
> **Known limitations:** `ENCODED` is still an ASCII '0'/'1' bitstring (no packed binary, no frame or CRC; that is P4), and the integrity hash is FNV-1a, not SHA-256. This repo is an Entropy-stage/verification reference, not a CT-01 wire format.
>
> **Scope:** `MODE=` is reference metadata, not a wire format. ct01's Entropy-stage input is post-LZ77, so absolute ratios here won't match ct01 end-to-end; the stored rule here applies to raw input. The decoder is not hardened for untrusted input yet (it only decodes its own in-process output); full hardening comes in P4.
