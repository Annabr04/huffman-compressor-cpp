# C++ Huffman Compressor (CT-01 / QuietLink compatible extensions)

Simple Huffman coding for lossless text/byte compression, extended with:
- File I/O
- Round-trip integrity verification
- Diagnostic-mode lossless lock
- Metadata output for device/app transparency

See HuffmanCompressor/README.md for usage.

> **P2:** Code lengths mirror CT-01's `huffman.cpp` exactly: deterministic tie-breaking by a unique (weight, node id) key, length-limited to 15 bits by (x+1)/2 frequency flattening, and DEFLATE-style canonical codes by (length, symbol). Encoded output is byte-identical across compilers (verified with zig c++ and g++). If Huffman would not be smaller than the raw input (CT-01's varint + 128-byte table + packed bits >= raw size), the payload is stored raw (`MODE=STORED`).
>
> **Known limitations:** `ENCODED` is still an ASCII '0'/'1' bitstring (no packed binary, no frame or CRC; that is P4). This repo is an Entropy-stage/verification reference, not a CT-01 wire format.
>
> **Integrity hash:** SHA-256 (self-contained `HuffmanCompressor/src/sha256.hpp`; requires C++17) of the original vs the decoded bytes, written as `INTEGRITY_HASH=sha256:<hex>`. It detects accidental corruption only; it is **not a MAC**, and authenticity is out of scope for this repo (in CT-01, at-rest authenticity comes from the AES-256-GCM sealed buffer/log; in-transit authenticity (TLS / per-frame MAC) is open pre-pilot). Per the UX contract the hash is never shown on dashboards.
>
> **Metadata key change (for parsers, e.g. Data Engineer / UX):** `ORIGINAL_HASH=<16-hex FNV-1a>` was replaced by `INTEGRITY_HASH=sha256:<64-hex>` (same position, line 2). The old key is no longer emitted.
>
> **Scope:** `MODE=` is reference metadata, not a wire format. ct01's Entropy-stage input is post-LZ77, so absolute ratios here won't match ct01 end-to-end; the stored rule here applies to raw input. The decoder is not hardened for untrusted input yet (it only decodes its own in-process output); full hardening comes in P4.
