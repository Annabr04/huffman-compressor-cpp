# HuffmanCompressor (C++) — CT-01 Compatible Mode

A simple C++ Huffman coding implementation extended for device/app integration concepts used in the CT-01 / QuietLink style edge compression pipeline.

## Features
- Frequency counting and canonical, length-limited (max 15 bits) Huffman codes identical to CT-01's `huffman.cpp`
- Stored bypass (`MODE=STORED`) when Huffman would not be smaller than the raw input
- Encoding / decoding with round-trip verification
- Lightweight integrity hash (FNV-1a) + exact decode match
- `--diagnostic` flag that enforces lossless lock (no lossy path)
- File input/output for synthetic payloads or sensor-stream style data
- Metadata output suitable for a companion dashboard or device log

## Build & Run

```bash
g++ -std=c++17 -O2 -Wall -o huff HuffmanCompressor/src/main.cpp
```

### Interactive / stdin
```bash
./huff                 # type data, end with Ctrl-D
./huff < sample.bin    # stdin is read as raw bytes until EOF
```
The prompt goes to stderr and only when stdin is a terminal, so piped stdout stays clean. Empty stdin is a valid round-trip, exactly like an empty `--input` file (`MODE=STORED`, empty `ENCODED`, rc 0).

### File mode (device-style)
```bash
./huff --input sample.txt --output payload.meta --diagnostic
```

This produces a small metadata file containing the mode (`MODE=HUFFMAN` or `MODE=STORED`), the encoded bits (ASCII '0'/'1'; in STORED mode each raw byte as 8 bits, MSB first), original hash, and integrity result — useful as a reference for the Entropy + Verify stages of a medical data compression device.

## Reference scope (not a CT-01 wire format)
- `MODE=` (`HUFFMAN` / `STORED`) and the other `KEY=value` lines are reference metadata for tests and dashboards, not a wire format. The CT-01 wire format (CT1F frame + CRC-32) lives only in ct01.
- ct01's Entropy stage runs on post-LZ77 data, so absolute compression ratios here will not match ct01 end-to-end. The stored rule here (Huffman only if varint + 128-byte table + packed bits < input size) is applied to the raw input.
- The decoder only decodes the binary's own just-produced output in-process (no CLI path reads external `ENCODED` or metadata). It is not hardened for untrusted input yet (no Kraft/over-subscription or padding rejection); full hardening comes in P4.
- **P4 TODO (external decode):** when a decode path for received data lands, keep the per-symbol `len <= 15` check and add Kraft `<= 1` (over-subscription) validation on the received length table, plus ct01's nonzero-padding and trailing-bits rejection, with negative tests.

## Integration Notes
- Diagnostic mode locks the path to lossless (matching CT-01 behavior).
- Exit code: 0 on a verified round-trip, 2 on any round-trip mismatch, 1 if the input file cannot be opened.
- Test hooks: `--decoded-output FILE` (decoded bytes) and `--dump-codes FILE` (`sym len` per used symbol).
- Integrity failure is reported and can be mapped to the device fault path (send uncompressed + notify).
- Suitable as a reference/oracle component or starting point for a production C++ core entropy stage.
- Synthetic data only; no PHI handling is implemented or intended here.

Maintained with weekly capability reviews aligned to the CT-01 project.
