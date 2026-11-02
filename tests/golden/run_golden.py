#!/usr/bin/env python3
"""CT-01 golden-vector round-trip test (synthetic data only, no PHI).

Usage:
  run_golden.py --generate            # (re)write generated vectors + SHA256SUMS
  run_golden.py --bin PATH            # verify vectors, run binary, exit nonzero on any failure
Vectors marked 'legacy' are the 2026-11-02 review vectors, pinned by hash.
Generated vectors are deterministic (Python random.Random, SEED below).
Exit: 0 = all PASS/XFAIL, 1 = any FAIL / mismatch / unexpected pass.
"""
import argparse, hashlib, heapq, os, random, subprocess, sys, tempfile

SEED = 20261102          # recorded PRNG seed for random_1mib
HERE = os.path.dirname(os.path.abspath(__file__))
VDIR = os.path.join(HERE, "vectors")
SUMS = os.path.join(HERE, "SHA256SUMS")
MAX_CODE_LEN = 15

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
    return v

LEGACY = ["empty.bin", "one.bin", "zeros.bin", "highent.bin", "repet.bin", "fib.bin", "nul_mixed.bin"]
# expected-fail: max Huffman code length > 15 until P2 length-limiting lands
XFAIL_MAXLEN = {"fib.bin", "fib26_maxlen.bin"}

def sha(b): return hashlib.sha256(b).hexdigest()

def huff_maxlen(data):
    """Max code length of an unrestricted Huffman tree for data (mirrors the
    binary's algorithm; binary has no --dump-codes yet)."""
    f = {}
    for c in data: f[c] = f.get(c, 0) + 1
    if len(f) <= 1: return 1 if f else 0
    h = [(n, i, 0) for i, n in enumerate(f.values())]; heapq.heapify(h); k = len(h)
    while len(h) > 1:
        a, b = heapq.heappop(h), heapq.heappop(h)
        k += 1; heapq.heappush(h, (a[0] + b[0], k, max(a[2], b[2]) + 1))
    return h[0][2]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--generate", action="store_true")
    ap.add_argument("--bin")
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
            p = subprocess.run([a.bin, "--diagnostic", "--input", os.path.join(VDIR, n), "--output", out],
                               capture_output=True)
            out_exists = os.path.exists(out)   # must be checked before td is deleted
            meta = open(out, "rb").read() if out_exists else b""
        integ = b"INTEGRITY=PASS" in meta
        enc = b""
        for line in meta.split(b"\n"):
            if line.startswith(b"ENCODED="): enc = line[8:]
        ok = p.returncode == 0 and integ and out_exists and meta != b""
        if n == "empty.bin":
            ok = ok and enc == b""
        status = "PASS" if ok else "FAIL"
        detail = f"rc={p.returncode} integrity={'PASS' if integ else 'FAIL/none'} bits={len(enc)}"
        if n in XFAIL_MAXLEN:
            ml = huff_maxlen(data)
            if ok and ml <= MAX_CODE_LEN: status = "XPASS(unexpected)"; fails += 1
            elif ok: status = "XFAIL"; detail += f" maxlen={ml}>{MAX_CODE_LEN} (round-trip ok; awaiting P2 length-limit)"
            else: fails += 1
        elif not ok: fails += 1
        print(f"{status:18} {n:18} sha256={h[:16]} {detail}")
    print(f"RESULT: {'FAIL' if fails else 'OK'} ({fails} failure(s))")
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
