#!/usr/bin/env python3
"""CT-01 golden-vector round-trip test (synthetic data only, no PHI).

Usage:
  run_golden.py --generate            # (re)write generated vectors + SHA256SUMS
  run_golden.py --bin PATH            # verify vectors, run binary, exit nonzero on any failure
Vectors marked 'legacy' are the 2026-11-02 review vectors, pinned by hash.
Generated vectors are deterministic (Python random.Random, SEED below).
Exit: 0 = all PASS/XFAIL, 1 = any FAIL / mismatch / unexpected pass.
"""
import argparse, hashlib, os, random, subprocess, sys, tempfile

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
    ap.add_argument("--max-code-len", type=int, default=MAX_CODE_LEN,
                    help="code-length limit (default 15); override only for self-test")
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
            dec = os.path.join(td, "decoded.bin")
            cdump = os.path.join(td, "codes.txt")
            p = subprocess.run([a.bin, "--diagnostic", "--input", os.path.join(VDIR, n), "--output", out,
                                "--decoded-output", dec, "--dump-codes", cdump], capture_output=True)
            out_exists = os.path.exists(out)   # must be checked before td is deleted
            meta = open(out, "rb").read() if out_exists else b""
            decoded = open(dec, "rb").read() if os.path.exists(dec) else None
            codes = read_dump(cdump)
        integ = b"INTEGRITY=PASS" in meta
        enc = b""
        for line in meta.split(b"\n"):
            if line.startswith(b"ENCODED="): enc = line[8:]
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
        if n in XFAIL_MAXLEN:
            if ok and ml <= a.max_code_len: status = "XPASS(unexpected)"; fails += 1; detail += " -> remove XFAIL marker"
            elif ok: status = "XFAIL"; detail += f">{a.max_code_len} (round-trip ok; awaiting P2 length-limit)"
            else: fails += 1
        elif ok and ml > a.max_code_len:
            status = "FAIL"; fails += 1; detail += f">{a.max_code_len}"
        elif not ok: fails += 1
        print(f"{status:18} {n:18} sha256={h[:16]} {detail}")
    print(f"RESULT: {'FAIL' if fails else 'OK'} ({fails} failure(s))")
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
