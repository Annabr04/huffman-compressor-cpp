#!/usr/bin/env python3
"""CT-01 golden-vector round-trip test (synthetic data only, no PHI).

Usage:
  run_golden.py --generate            # (re)write generated vectors + SHA256SUMS
  run_golden.py --bin PATH            # verify vectors, run binary, exit nonzero on any failure
Vectors marked 'legacy' are the 2026-11-02 review vectors, pinned by hash.
Generated vectors are deterministic (Python random.Random, SEED below).
Exit: 0 = all PASS, 1 = any FAIL / mismatch.
P2 hard assertions: every code length <= 15 (ct01 kHuffMaxBits), complete
prefix code (Kraft sum == 1, single symbol excepted), and the binary's MODE
matches ct01's rule: Huffman only if varint(n) + 128 + ceil(bits/8) < n,
otherwise STORED (near_expand/highent must be STORED).
ENCODED is compiler-stable since P2, so its SHA-256 is pinned per vector in
ENCODED_SHA256SUMS (D1): any bit flip or code/tie/flattening change FAILs.
Re-pin only for a reviewed codec change: run_golden.py --bin PATH --pin-encoded.
Every vector is also fed via stdin (no --input): same metadata as file mode,
exact decoded bytes, rc 0, and nothing on stderr (no prompt when not a tty).
Empty stdin must round-trip like empty.bin (MODE=STORED, empty ENCODED, rc 0).
"""
import argparse, hashlib, os, random, subprocess, sys, tempfile
from collections import Counter
from fractions import Fraction

SEED = 20261102          # recorded PRNG seed for random_1mib
HERE = os.path.dirname(os.path.abspath(__file__))
VDIR = os.path.join(HERE, "vectors")
SUMS = os.path.join(HERE, "SHA256SUMS")
ENC_SUMS = os.path.join(HERE, "ENCODED_SHA256SUMS")
MAX_CODE_LEN = 15        # ct01 kHuffMaxBits; hard limit, not overridable

def fib_weights(n):
    a, b, out = 1, 1, []
    for _ in range(n):
        out.append(a); a, b = b, a + b
    return out

def gen():
    v = {}
    v["all256.bin"] = bytes(range(256))
    v["nul_first.bin"] = b"\x00" + b"abcdefgh" * 8
    v["nul_last.bin"] = b"abcdefgh" * 8 + b"\x00"
    v["single_nul.bin"] = b"\x00" * 64
    v["two_sym.bin"] = b"B" + b"A" * 4095            # 1 short + 1 long symbol
    v["near_expand.bin"] = bytes(range(256)) * 16      # flat 256-symbol: every code 8 bits
    w = fib_weights(26)                                # 26-symbol Fibonacci alphabet
    v["fib26_maxlen.bin"] = b"".join(bytes([0x41 + i]) * c for i, c in enumerate(w))
    v["random_1mib.bin"] = random.Random(SEED).randbytes(1 << 20)
    # D3: MODE boundary. Two symbols, 1 bit each: packed = 2 + 128 + ceil(n/8).
    v["two_149.bin"] = b"B" + b"A" * 148               # packed 149 == n -> STORED (>=)
    v["two_150.bin"] = b"B" + b"A" * 149               # packed 149 <  n -> HUFFMAN
    # D2: heavy halving. Weights 1,1,2,4,...,2^17 (19 symbols): plain max len 18,
    # needs 3 rounds of (x+1)/2 flattening, ends at exactly 15 bits.
    v["halving_pow2_19.bin"] = b"".join(bytes([0x41 + i]) * (1 if i == 0 else 1 << (i - 1)) for i in range(19))
    return v

LEGACY = ["empty.bin", "one.bin", "zeros.bin", "highent.bin", "repet.bin", "fib.bin", "nul_mixed.bin"]
# ct01 stored bypass must trigger on these (Huffman would not be smaller)...
MUST_STORE = {"near_expand.bin", "highent.bin", "empty.bin", "two_149.bin"}
# ...and must NOT trigger on these (exercise the Huffman path, incl. length limiting)
MUST_HUFFMAN = {"fib.bin", "fib26_maxlen.bin", "zeros.bin", "two_sym.bin", "repet.bin", "two_150.bin",
                "halving_pow2_19.bin"}
EXPECT_MAXLEN = {"halving_pow2_19.bin": 15}

def ct01_packed_size(n, bits):
    """ct01 Huffman stream size: varint(n) + 128-byte length table + packed bits."""
    vlen = 1
    while n >= 0x80: n >>= 7; vlen += 1
    return vlen + 128 + (bits + 7) // 8

def sha(b): return hashlib.sha256(b).hexdigest()

def read_dump(path):
    """Parse binary's --dump-codes output ('sym len' lines) -> {sym: len}."""
    if not os.path.exists(path): return None
    d = {}
    for line in open(path):
        sym, ln = line.split(); d[int(sym)] = int(ln)
    return d

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--generate", action="store_true")
    ap.add_argument("--bin")
    ap.add_argument("--pin-encoded", action="store_true",
                    help="(re)write ENCODED_SHA256SUMS from --bin; only after a reviewed codec change")
    a = ap.parse_args()
    if a.generate:
        os.makedirs(VDIR, exist_ok=True)
        for n, b in gen().items():
            open(os.path.join(VDIR, n), "wb").write(b)
        names = sorted(os.listdir(VDIR))
        with open(SUMS, "w") as fh:
            for n in names:
                if n.endswith(".bin"):
                    fh.write(f"{sha(open(os.path.join(VDIR, n), 'rb').read())}  {n}\n")
        print(f"wrote {len(names)} vectors + SHA256SUMS (seed={SEED})"); return 0
    if not a.bin: ap.error("--bin or --generate required")
    fails = 0
    file_meta = {}
    enc_pins = {}
    if os.path.exists(ENC_SUMS):
        enc_pins = {l.split()[1]: l.split()[0] for l in open(ENC_SUMS) if l.strip()}
    new_pins = {}
    expected = {l.split()[1]: l.split()[0] for l in open(SUMS) if l.strip()}
    gv = gen()
    for n in LEGACY + list(gv):
        if n not in expected: print(f"MISSING-SUM {n}"); fails += 1
    for n, h in sorted(expected.items()):
        data = open(os.path.join(VDIR, n), "rb").read()
        if sha(data) != h: print(f"SUM-MISMATCH {n}"); fails += 1; continue
        if n in gv and gv[n] != data: print(f"GENERATOR-MISMATCH {n}"); fails += 1; continue
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, "out.txt")
            dec = os.path.join(td, "decoded.bin")
            cdump = os.path.join(td, "codes.txt")
            p = subprocess.run([a.bin, "--diagnostic", "--input", os.path.join(VDIR, n), "--output", out,
                                "--decoded-output", dec, "--dump-codes", cdump], capture_output=True)
            out_exists = os.path.exists(out)   # must be checked before td is deleted
            meta = open(out, "rb").read() if out_exists else b""
            decoded = open(dec, "rb").read() if os.path.exists(dec) else None
            codes = read_dump(cdump)
        file_meta[n] = meta
        integ = b"INTEGRITY=PASS" in meta
        enc = b""; mode = None
        for line in meta.split(b"\n"):
            if line.startswith(b"ENCODED="): enc = line[8:]
            if line.startswith(b"MODE="): mode = line[5:].decode(errors="replace")
        # independent round-trip check: decoded bytes must hash to SHA256SUMS entry
        rt = decoded is not None and sha(decoded) == h and decoded == data
        ok = p.returncode == 0 and integ and out_exists and meta != b"" and rt
        if n == "empty.bin":
            ok = ok and enc == b""
        status = "PASS" if ok else "FAIL"
        detail = f"rc={p.returncode} integrity={'PASS' if integ else 'FAIL/none'} roundtrip={'OK' if rt else ('NO-DECODED' if decoded is None else 'MISMATCH')} bits={len(enc)}"
        # code lengths come from the binary itself (--dump-codes)
        if codes is None or set(codes) != set(data):
            ok = False; status = "FAIL"; detail += " codes=MISSING/MISMATCH"; ml = None
        else:
            ml = max(codes.values(), default=0); detail += f" maxlen={ml}"
        problems = []
        if ml is not None:
            # hard assertion: max code length <= 15
            if ml > MAX_CODE_LEN: problems.append(f"maxlen>{MAX_CODE_LEN}")
            if codes and len(codes) > 1 and sum(Fraction(1, 2 ** l) for l in codes.values()) != 1:
                problems.append("kraft!=1")
            if len(codes) == 1 and list(codes.values()) != [1]: problems.append("single-sym-len!=1")
            freq = Counter(data)
            hbits = sum(freq[s] * codes[s] for s in codes)
            want = "STORED" if len(data) == 0 or ct01_packed_size(len(data), hbits) >= len(data) else "HUFFMAN"
            detail += f" mode={mode}"
            if mode != want: problems.append(f"mode!={want}")
            if n in MUST_STORE and mode != "STORED": problems.append("stored-bypass-missing")
            if n in MUST_HUFFMAN and mode != "HUFFMAN": problems.append("huffman-expected")
            if len(enc) != (8 * len(data) if want == "STORED" else hbits): problems.append("bitcount")
            if n in EXPECT_MAXLEN and ml != EXPECT_MAXLEN[n]: problems.append(f"maxlen!={EXPECT_MAXLEN[n]}")
        # D1: pinned ENCODED content (compiler-stable since P2)
        eh = sha(enc); new_pins[n] = eh
        if not a.pin_encoded:
            if n not in enc_pins: problems.append("encoded-pin-missing")
            elif enc_pins[n] != eh: problems.append("encoded-sha!=pinned")
        if problems:
            ok = False; status = "FAIL"; detail += " " + ",".join(problems)
        if not ok: fails += 1
        print(f"{status:18} {n:18} sha256={h[:16]} {detail}")
    # stdin path (binary read until EOF; empty stdin is a valid round-trip)
    for n in sorted(expected):
        data = open(os.path.join(VDIR, n), "rb").read()
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, "out.txt"); dec = os.path.join(td, "decoded.bin")
            p = subprocess.run([a.bin, "--diagnostic", "--output", out, "--decoded-output", dec],
                               input=data, capture_output=True)
            meta = open(out, "rb").read() if os.path.exists(out) else None
            decoded = open(dec, "rb").read() if os.path.exists(dec) else None
        problems = []
        if p.returncode != 0: problems.append(f"rc={p.returncode}")
        if p.stderr: problems.append("stderr-not-empty")
        if decoded != data: problems.append("roundtrip")
        if meta is None or meta != file_meta.get(n): problems.append("meta!=file-mode")
        if n == "empty.bin" and (meta is None or b"MODE=STORED\n" not in meta or b"ENCODED=\n" not in meta):
            problems.append("empty-stdin-not-stored")
        if problems: fails += 1
        print(f"{'FAIL' if problems else 'PASS':18} stdin:{n:12} {','.join(problems) or 'rc=0 meta==file-mode roundtrip=OK'}")
    if a.pin_encoded:
        if fails: print("not pinning: failures present")
        else:
            with open(ENC_SUMS, "w") as fh:
                for n in sorted(new_pins): fh.write(f"{new_pins[n]}  {n}\n")
            print(f"pinned {len(new_pins)} ENCODED sha256 -> {os.path.basename(ENC_SUMS)}")
    print(f"RESULT: {'FAIL' if fails else 'OK'} ({fails} failure(s))")
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
