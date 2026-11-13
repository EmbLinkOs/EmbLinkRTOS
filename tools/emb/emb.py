#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""emb: build, flash, watch and test an EmbLinkRTOS image (BLD-005, SPEC-015 §10).

    emb build -b nucleo_f446re -S examples/logging        # configure and build, per-board defaults
    emb build -b nucleo_f446re -S examples/logging --framed --clang
    emb flash -B build/logging-nucleo_f446re               # the board's own probe (hw.json)
    emb monitor -B build/logging-nucleo_f446re             # the console: frames decoded
    emb hil -B build/nucleo-hil -R test_log                # the suite on the board (ctest)
    emb trace dump -B build/tracing-nucleo_f446re --ctf out/trace --perfetto out/trace.json

A build directory knows its board, its image and its console (CMakeCache.txt, the board's
hw.json), so the other commands need only -B. The monitor shows the console's text as it comes,
and on a framed console (A127) decodes each channel: the log's records with the image's ELF
(tools/logdec), the crash record (tools/crashdec), the trace's packets (tools/trace, kept with
--trace-out), the shell's text. Bytes that are not a frame are shown as they are.
"""
import argparse
import os
import select
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for sub in ("transport", "logdec", "crashdec", "trace", "hil"):
    sys.path.insert(0, str(HERE.parent / sub))
import frame as framing  # noqa: E402

CH_LOG, CH_TRACE, CH_CRASH, CH_SHELL = 0, 1, 2, 3
CLANG_TOOLCHAIN = {"cortex_m": "arm-none-eabi-clang", "riscv": "riscv-clang", "avr": "avr-clang",
                   "xtensa": "xtensa-esp32-clang", "native": "native-clang"}


class EmbError(Exception):
    pass


# --------------------------------------------------------------------------- the build tree

def cache(build):
    """The CMake cache of a build directory: {name: value}."""
    path = Path(build) / "CMakeCache.txt"
    if not path.is_file():
        raise EmbError(f"{build}: not a configured build directory (no CMakeCache.txt): run emb build")
    out = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            out[key.split(":", 1)[0]] = value
    return out


def board_dir(name):
    """A board of the tree by name: the directory whose board.yaml it is."""
    found = [p.parent for p in (ROOT / "boards").glob("*/*/board.yaml") if p.parent.name == name]
    if len(found) != 1:
        raise EmbError(f"board {name!r}: {'not found' if not found else 'more than one'} under boards/")
    return found[0]


def hw_json(build):
    """The board's generated hw.json for a build directory."""
    import json
    c = cache(build)
    for key in ("EMB_BOARD_HW_JSON",):
        if c.get(key) and Path(c[key]).is_file():
            return json.loads(Path(c[key]).read_text(encoding="utf-8"))
    bdir = Path(c.get("EMB_BOARD_DIR") or board_dir(c.get("EMB_BOARD", "")))
    for cand in (bdir / "hw.json", bdir / "generated" / "hw.json", bdir / "docs" / "hw.json"):
        if cand.is_file():
            return json.loads(cand.read_text(encoding="utf-8"))
    raise EmbError(f"{build}: no hw.json for board {c.get('EMB_BOARD')}")


def image(build, name=None):
    """The image a build made: the one named, or the only one at the top of the build tree."""
    b = Path(build)
    if name:
        p = Path(name)
        return p if p.is_file() else b / name
    project = cache(build).get("CMAKE_PROJECT_NAME", "")
    for cand in (b / f"{project}.elf", b / project):
        if project and cand.is_file():
            return cand
    elves = sorted(b.glob("*.elf"))
    if len(elves) == 1:
        return elves[0]
    raise EmbError(f"{build}: name the image (found {len(elves)} at the top of the build directory)")


def console_port(build=None, given=None):
    """The console's serial device: as given, as the build was configured (EMB_HIL_PORT), as
    EMB_PORT says, or the only USB serial device there is."""
    if given:
        return given
    if build:
        p = cache(build).get("EMB_HIL_PORT")
        if p:
            return p
    if os.environ.get("EMB_PORT"):
        return os.environ["EMB_PORT"]
    devs = sorted(str(p) for pat in ("cu.usbmodem*", "cu.usbserial*", "ttyACM*", "ttyUSB*")
                  for p in Path("/dev").glob(pat))
    if len(devs) == 1:
        return devs[0]
    raise EmbError(f"which console? found {devs or 'no serial device'}: give --port or set EMB_PORT")


# ----------------------------------------------------------------------------- commands

def build_command(args):
    """The cmake invocations for emb build, as lists."""
    src = Path(args.source).resolve()
    out = Path(args.build or f"build/{src.name}-{args.board}")
    conf = ["cmake", "-S", str(src), "-B", str(out), "-G", "Ninja", f"-DEMB_BOARD={args.board}"]
    sets = list(args.set or [])
    if args.framed:
        sets.append("EMB_CONSOLE_FRAMED=y")
    if sets:
        conf.append("-DEMB_SET=" + ";".join(sets))
    if args.hil:
        conf.append("-DEMB_TEST_EMULATOR=hil")
    if args.port:
        conf.append(f"-DEMB_HIL_PORT={args.port}")
    if args.clang:
        import json
        hw = json.loads((board_dir(args.board) / "generated" / "hw.json").read_text(encoding="utf-8"))
        tc = CLANG_TOOLCHAIN.get(hw["soc"]["arch"]["port"])
        if tc is None:
            raise EmbError(f"no Clang toolchain file for {hw['soc']['arch']['port']}")
        conf.append(f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake' / 'toolchains' / (tc + '.cmake')}")
    if src != ROOT:
        conf.append(f"-DEmbLinkRTOS_DIR={ROOT / 'cmake'}")
    try:
        import yaml  # noqa: F401  the build's configuration step needs PyYAML
        conf.append(f"-DPython3_EXECUTABLE={sys.executable}")
    except ImportError:
        pass
    conf += [f"-D{d}" for d in (args.define or [])]
    return [conf, ["cmake", "--build", str(out)]], out


def run(cmd, dry):
    print("$ " + " ".join(shlex.quote(c) for c in cmd), file=sys.stderr)
    if dry:
        return 0
    return subprocess.run(cmd).returncode


def cmd_build(args):
    cmds, out = build_command(args)
    for c in cmds:
        rc = run(c, args.dry_run)
        if rc:
            return rc
    if not args.dry_run:
        print(f"emb: built {out}", file=sys.stderr)
    return 0


def cmd_flash(args):
    import run_hil
    hw = hw_json(args.build)
    img = image(args.build, args.image)
    port = console_port(args.build, args.port) if ("avrdude" in hw["board"].get("debug_probe", {}) or
                                                    "esp" in hw["board"].get("debug_probe", {})) else None
    if run_hil.uses_picotool(hw) and not args.dry_run:
        problem = run_hil.wait_bootsel(args.timeout)
        if problem:
            raise EmbError(problem)
    cmd = run_hil.flash_command(hw, img, args.flash_cmd, port)
    return run(cmd, args.dry_run)


class Console:
    """The console's bytes into what to show: text as it comes, frames when they end. A framed
    console precedes each frame with a zero (A127), so bytes after a zero wait for the next zero
    and are a frame if they decode; bytes before any zero are a plain console's text."""

    def __init__(self, max_payload=4096):
        self.framing = False
        self.buf = bytearray()
        self.max = max_payload + 3
        self.frames = self.errors = 0

    def feed(self, data):
        out = []
        for b in data:
            if not self.framing:
                if b == 0:
                    if self.buf:
                        out.append(("text", bytes(self.buf)))
                    self.buf = bytearray()
                    self.framing = True
                else:
                    self.buf.append(b)
                continue
            if b != 0:
                self.buf.append(b)
                if len(self.buf) > self.max * 2:  # no frame is this long: text after all
                    out.append(("text", bytes(self.buf)))
                    self.buf = bytearray()
                continue
            if self.buf:
                try:
                    ch, payload = framing.decode(bytes(self.buf))
                    out.append(("frame", ch, payload))
                    self.frames += 1
                except framing.FrameError:
                    if framing._texty(self.buf):
                        out.append(("text", bytes(self.buf)))
                    else:
                        self.errors += 1
            self.buf = bytearray()
        if not self.framing and self.buf:  # a plain console: show it now
            out.append(("text", bytes(self.buf)))
            self.buf = bytearray()
        return out


class Monitor:
    """What each channel's payload shows as."""

    def __init__(self, elf=None, where=False, trace_out=None, write=sys.stdout.write):
        self.write = write
        self.stream = None
        self.trace_out = trace_out
        self.trace_packets = 0
        self.note = None
        if elf:
            import logdec
            try:
                self.stream = logdec.Stream(logdec.LogDb(elf), where)
            except logdec.LogError as e:
                self.note = f"emb: no log records decoded: {e}"

    def show(self, item):
        if item[0] == "text":
            self.write(item[1].decode(errors="replace"))
            return
        _, ch, payload = item
        if ch == CH_SHELL:
            self.write(payload.decode(errors="replace"))
        elif ch == CH_LOG:
            if self.stream is None:
                self.write(f"(log records, {len(payload)} bytes: give --elf to decode them)\n")
            else:
                for line in self.stream.records(payload):
                    self.write(line + "\n")
        elif ch == CH_TRACE:
            self.trace_packets += 1
            if self.trace_out is not None:
                self.trace_out.write(b"\x00" + framing.encode(CH_TRACE, payload))
        elif ch == CH_CRASH:
            import crashdec
            try:
                self.write(crashdec.text(crashdec.parse(payload)) + "\n")
            except Exception as e:  # noqa: BLE001  a damaged record is shown, not fatal
                self.write(f"(a crash record that does not decode: {e})\n")
        else:
            self.write(f"(channel {ch}: {payload.hex()})\n")


def read_console(port, baud, seconds, until, sink):
    """Reads the console until --seconds pass, --until's text is seen, or Ctrl-C."""
    import run_hil
    fd = run_hil.open_console(port, baud)
    end = time.monotonic() + seconds if seconds else None
    seen = b""
    try:
        while end is None or time.monotonic() < end:
            r, _, _ = select.select([fd], [], [], 0.2)
            if not r:
                continue
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            sink(chunk)
            if until:
                seen = (seen + chunk)[-(len(until) + 4096):]
                if until.encode() in seen:
                    return True
    except KeyboardInterrupt:
        pass
    finally:
        os.close(fd)
    return not until


def cmd_monitor(args):
    hw = hw_json(args.build) if args.build else None
    baud = args.baud or (hw["board"]["console"]["baud"] if hw else 115200)
    port = console_port(args.build, args.port)
    elf = args.elf or (str(image(args.build)) if args.build else None)
    trace_out = open(args.trace_out, "wb") if args.trace_out else None  # noqa: SIM115
    mon = Monitor(elf, args.where, trace_out)
    if mon.note:
        print(mon.note, file=sys.stderr)
    con = Console()
    log = open(args.log, "wb") if args.log else None  # noqa: SIM115

    def sink(chunk):
        if log:
            log.write(chunk)
        for item in con.feed(chunk):
            mon.show(item)
        sys.stdout.flush()

    if args.input:
        sink(Path(args.input).read_bytes())
        ok = True
    else:
        print(f"emb: {port} at {baud} baud; Ctrl-C ends", file=sys.stderr)
        ok = read_console(port, baud, args.seconds, args.until, sink)
    for f in (log, trace_out):
        if f:
            f.close()
    print(f"emb: {con.frames} frames, {con.errors} damaged, {mon.trace_packets} trace packets", file=sys.stderr)
    return 0 if ok else 1


def cmd_hil(args):
    cmd = ["ctest", "--test-dir", str(args.build), "--output-on-failure"]
    if args.regex:
        cmd += ["-R", args.regex]
    if not args.dry_run:
        c = cache(args.build)
        if c.get("EMB_TEST_EMULATOR") != "hil":
            raise EmbError(f"{args.build} was not configured for the board: emb build --hil --port ...")
    return run(cmd, args.dry_run)


def cmd_trace_dump(args):
    """The trace packets an image sends (emb_trace_dump()) from the console or a capture, written
    as CTF and Perfetto's JSON."""
    import trace
    if args.input:
        data = Path(args.input).read_bytes()
    else:
        hw = hw_json(args.build)
        chunks = []
        read_console(console_port(args.build, args.port), hw["board"]["console"]["baud"], args.seconds,
                     args.until, chunks.append)
        data = b"".join(chunks)
    tr, dec = trace.read_capture(data)
    if not tr.events:
        raise EmbError("no trace packets on channel 1: does the image call emb_trace_dump()?")
    cat = trace.catalogue()
    if args.ctf:
        trace.ctf(tr, cat, args.ctf)
    if args.perfetto:
        import json
        Path(args.perfetto).write_text(json.dumps(trace.perfetto(tr, cat)), encoding="utf-8")
    if not (args.ctf or args.perfetto):
        sys.stdout.write(trace.text(tr, cat))
    print(f"emb: {len(tr.events)} events, {len(tr.names)} thread names, {tr.gaps} gaps", file=sys.stderr)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="emb", description=__doc__.split("\n")[0],
                                 epilog=__doc__.split("\n\n", 1)[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    b = sub.add_parser("build", help="configure and build for a board")
    b.add_argument("-b", "--board", required=True)
    b.add_argument("-S", "--source", default=".", help="the application, or the RTOS tree for its tests")
    b.add_argument("-B", "--build", help="the build directory (default build/<source>-<board>)")
    b.add_argument("--clang", action="store_true", help="Clang and LLD instead of the board's GCC")
    b.add_argument("--framed", action="store_true", help="the framed console (A127)")
    b.add_argument("--hil", action="store_true", help="run the tests on the board (EMB_TEST_EMULATOR=hil)")
    b.add_argument("--port", help="the board's console, for --hil")
    b.add_argument("--set", action="append", metavar="SYMBOL=VALUE", help="a Kconfig symbol (repeatable)")
    b.add_argument("-D", "--define", action="append", metavar="VAR=VALUE", help="a CMake variable")
    b.add_argument("-n", "--dry-run", action="store_true", help="print the commands only")
    b.set_defaults(func=cmd_build)

    f = sub.add_parser("flash", help="program the image with the board's probe")
    f.add_argument("-B", "--build", required=True)
    f.add_argument("image", nargs="?", help="default: the build's only image")
    f.add_argument("--port", help="the serial port, for boards flashed through it")
    f.add_argument("--flash-cmd", help="a command of your own, {image} and {port} replaced")
    f.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for BOOTSEL")
    f.add_argument("-n", "--dry-run", action="store_true")
    f.set_defaults(func=cmd_flash)

    m = sub.add_parser("monitor", help="show the console, frames decoded")
    m.add_argument("-B", "--build", help="the build: its image, its board's console")
    m.add_argument("--port")
    m.add_argument("--baud", type=int)
    m.add_argument("--elf", help="the image whose log records to decode (default: the build's)")
    m.add_argument("--where", action="store_true", help="each log record's file and line")
    m.add_argument("--seconds", type=float, help="stop after this long")
    m.add_argument("--until", help="stop once this text is seen")
    m.add_argument("--log", help="also keep the raw bytes")
    m.add_argument("--trace-out", help="keep the trace packets, for emb trace dump --input")
    m.add_argument("--input", help="a capture instead of the port")
    m.set_defaults(func=cmd_monitor)

    h = sub.add_parser("hil", help="run a build's tests on the board (ctest)")
    h.add_argument("-B", "--build", required=True)
    h.add_argument("-R", "--regex", help="the tests whose names match")
    h.add_argument("-n", "--dry-run", action="store_true")
    h.set_defaults(func=cmd_hil)

    t = sub.add_parser("trace", help="the trace ring")
    tsub = t.add_subparsers(dest="trace_command", required=True)
    td = tsub.add_parser("dump", help="the packets emb_trace_dump() sends, as CTF or Perfetto JSON")
    td.add_argument("-B", "--build")
    td.add_argument("--port")
    td.add_argument("--input", help="a capture (emb monitor --log or --trace-out) instead of the port")
    td.add_argument("--seconds", type=float, default=30.0)
    td.add_argument("--until", help="stop once this text is seen")
    td.add_argument("--ctf", metavar="DIR")
    td.add_argument("--perfetto", metavar="FILE")
    td.set_defaults(func=cmd_trace_dump)

    args = ap.parse_args(argv)
    try:
        return args.func(args)
    except EmbError as e:
        print(f"emb: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
