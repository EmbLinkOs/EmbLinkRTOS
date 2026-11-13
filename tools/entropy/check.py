#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The host's sanity checks of the entropy a board printed (test_entropy_board, A124): the
ENTROPY lines of a run's log, the conditioned sample checked against the SHA-256 the board gave
it, then the monobit test, the byte frequencies by chi-square, the runs test (NIST SP 800-22
sections 2.1 and 2.3), the serial correlation of the bytes, and how far zlib compresses them;
and the raw source's min-entropy, NIST SP 800-90B's most-common-value estimate from the counts
the board printed. These are checks of the driver and of gross faults in a source, not a
certification of the source."""
import argparse
import hashlib
import math
import re
import sys
import zlib

LINE = re.compile(r"^ENTROPY ([0-9a-f]{64})\s*$")
SUM = re.compile(r"^ENTROPY sample (\d+) bytes sha256 ([0-9a-f]{64})\s*$")
RAW = re.compile(r"^ENTROPY raw (\d+) bytes, most common value (\d+) times")
MIN_ENTROPY = 4.0           # bits a byte: what conditioning 2:1 needs
Z_LIMIT = 5.0             # standard deviations: one false alarm in some 1.7 million per test
CHI_LIMITS = (150.0, 400.0)  # 255 degrees of freedom: mean 255, deviation 22.6
COMPRESSED_MIN = 0.99     # zlib's output, as a share of the input


class SampleError(Exception):
    pass


def sample_from(log_text):
    """The sample's bytes, checked against the board's SHA-256 of them."""
    chunks, want = [], None
    for line in log_text.splitlines():
        m = LINE.match(line.strip())
        if m:
            chunks.append(bytes.fromhex(m.group(1)))
            continue
        m = SUM.match(line.strip())
        if m:
            want = (int(m.group(1)), m.group(2))
    data = b"".join(chunks)
    if want is None:
        raise SampleError("no 'ENTROPY sample ... sha256' line: not a test_entropy_board log")
    if len(data) != want[0]:
        raise SampleError(f"{len(data)} bytes in the log, the board printed {want[0]}")
    if hashlib.sha256(data).hexdigest() != want[1]:
        raise SampleError("the sample's SHA-256 differs from the board's: a line was lost or changed")
    return data


def raw_min_entropy(log_text):
    """The raw source's min-entropy in bits a byte, NIST SP 800-90B 6.3.1's most-common-value
    estimate from the counts the board printed; None where the log has no raw line."""
    for line in log_text.splitlines():
        m = RAW.match(line.strip())
        if m:
            n, most = int(m.group(1)), int(m.group(2))
            p = most / n
            p_u = min(1.0, p + 2.576 * math.sqrt(p * (1 - p) / (n - 1)))
            return -math.log2(p_u)
    return None


def checks(data):
    """Each check's name, its value, and whether it passed."""
    n = len(data)
    bits = n * 8
    ones = sum(bin(b).count("1") for b in data)
    out = []
    z = abs(2 * ones - bits) / math.sqrt(bits)
    out.append(("monobit: ones of the bits", f"{ones} of {bits}, z = {z:.2f}", z < Z_LIMIT))
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    e = n / 256
    chi = sum((c - e) ** 2 for c in counts) / e
    out.append(("byte frequencies: chi-square, 255 degrees", f"{chi:.1f}",
                CHI_LIMITS[0] <= chi <= CHI_LIMITS[1]))
    pi = ones / bits
    stream = "".join(f"{b:08b}" for b in data)
    runs = 1 + sum(1 for i in range(1, bits) if stream[i] != stream[i - 1])
    zr = abs(runs - 2 * bits * pi * (1 - pi)) / (2 * math.sqrt(2 * bits) * pi * (1 - pi))
    out.append(("runs of equal bits", f"{runs}, z = {zr:.2f}", zr < Z_LIMIT))
    mean = sum(data) / n
    num = sum((data[i] - mean) * (data[i + 1] - mean) for i in range(n - 1))
    den = sum((b - mean) ** 2 for b in data)
    r = num / den if den else 1.0
    zs = abs(r) * math.sqrt(n)
    out.append(("serial correlation of the bytes", f"{r:+.5f}, z = {zs:.2f}", zs < Z_LIMIT))
    ratio = len(zlib.compress(data, 9)) / n
    out.append(("zlib's compression", f"{ratio:.4f} of the input", ratio >= COMPRESSED_MIN))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("log", nargs="+", help="test_entropy_board's log: a HIL record's .log")
    a = ap.parse_args(argv)
    failed = False
    for path in a.log:
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
            data = sample_from(text)
        except (OSError, SampleError) as e:
            print(f"{path}: {e}")
            failed = True
            continue
        print(f"{path}: {len(data)} bytes, their SHA-256 the board's")
        for name, value, ok in checks(data):
            print(f"  {'pass' if ok else 'FAIL'}  {name}: {value}")
            failed = failed or not ok
        h = raw_min_entropy(text)
        if h is not None:
            ok = h >= MIN_ENTROPY
            print(f"  {'pass' if ok else 'FAIL'}  the raw source's min-entropy (most common value): "
                  f"{h:.2f} bits a byte, at least {MIN_ENTROPY:.0f}")
            failed = failed or not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
