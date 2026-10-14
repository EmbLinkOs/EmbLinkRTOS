#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Install the pinned Linux x86-64 Doxygen release into the ignored build tree."""
import argparse
import hashlib
import platform
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION = "1.15.0"
URL = f"https://github.com/doxygen/doxygen/releases/download/Release_1_15_0/doxygen-{VERSION}.linux.bin.tar.gz"
# Published by RIOT's official doc/doxygen/Makefile for this exact release.
SHA512 = "0e99fb9a65f9fada2fe90ea0d1f940b8e58d62801f880614b440b9f3a9f82a5c1a8dd1d0719ee6a46f9612ea7447655d5c8494f63463fe7f1222987bdb1485a9"
INSTALL = ROOT / "build/tools/doxygen" / VERSION
BINARY = INSTALL / f"doxygen-{VERSION}/bin/doxygen"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Use an already downloaded archive; checksum still required")
    args = parser.parse_args()
    if BINARY.is_file():
        actual = subprocess.check_output([str(BINARY), "--version"], text=True).strip()
        if actual.split()[0] != VERSION:
            raise SystemExit(f"Unexpected Doxygen version: {actual}")
        print(f"Doxygen already installed: {actual}")
        return
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "AMD64"}:
        raise SystemExit("Automatic install supports Linux x86-64. Install Doxygen 1.15.0 with your trusted package manager and pass --doxygen to build.py.")
    INSTALL.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=INSTALL.parent) as temporary:
        work = Path(temporary)
        archive = work / "doxygen.tar.gz"
        if args.archive:
            shutil.copyfile(args.archive, archive)
        else:
            with urllib.request.urlopen(URL, timeout=60) as response, archive.open("wb") as target:
                shutil.copyfileobj(response, target)
        with archive.open("rb") as stream:
            actual_hash = hashlib.file_digest(stream, "sha512").hexdigest()
        if actual_hash != SHA512:
            raise SystemExit("Doxygen archive checksum mismatch; refusing installation")
        unpacked = work / "unpacked"
        unpacked.mkdir()
        with tarfile.open(archive) as source:
            source.extractall(unpacked, filter="data")
        if INSTALL.exists():
            raise SystemExit(f"Incomplete installation at {INSTALL}; inspect it before removing or replacing it")
        unpacked.rename(INSTALL)
    subprocess.run([str(BINARY), "--version"], check=True)


if __name__ == "__main__":
    main()
