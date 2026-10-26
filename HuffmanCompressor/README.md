# HuffmanCompressor (C++) — CT-01 Compatible Mode

A simple C++ Huffman coding implementation extended for device/app integration concepts used in the CT-01 / QuietLink style edge compression pipeline.

## Features
- Frequency counting and Huffman tree construction
- Encoding / decoding with round-trip verification
- Lightweight integrity hash (FNV-1a) + exact decode match
- `--diagnostic` flag that enforces lossless lock (no lossy path)
- File input/output for synthetic payloads or sensor-stream style data
- Metadata output suitable for a companion dashboard or device log

## Build & Run

```bash
cd HuffmanCompressor/src
g++ -std=c++17 main.cpp -o huff
```

### Interactive
```bash
./huff
```

### File mode (device-style)
```bash
./huff --input sample.txt --output payload.meta --diagnostic
```

This produces a small metadata file containing the encoded bits, original hash, and integrity result — useful as a reference for the Entropy + Verify stages of a medical data compression device.

## Integration Notes
- Diagnostic mode locks the path to lossless (matching CT-01 behavior).
- Integrity failure is reported and can be mapped to the device fault path (send uncompressed + notify).
- Suitable as a reference/oracle component or starting point for a production C++ core entropy stage.
- Synthetic data only; no PHI handling is implemented or intended here.

Maintained with weekly capability reviews aligned to the CT-01 project.
