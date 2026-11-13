#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Turn the trace ring's packets into CTF, Perfetto's JSON or text (SPEC-015 §3, OBS-006, A128).

    trace.py console.bin --ctf out/trace            # a CTF 1.8 trace: babeltrace2 out/trace
    trace.py console.bin --perfetto trace.json      # open in https://ui.perfetto.dev
    trace.py console.bin --text                     # one line per event

The input is a capture of a framed console; the packets are on channel 1. The event catalogue
is the enum in include/emb/trace.h: its names and its arguments' notes. A packet's events
carry the low 32 bits of the cycle counter, which this tool extends across its wraps, so two
events further apart than half the counter's period would be ambiguous: the periodic clock's
tick events keep them closer.
"""
import argparse
import json
import re
import struct
import sys
import uuid as uuidlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE.parent / "transport"))
import frame as framing  # noqa: E402

CH_TRACE = 1
KIND_EVENTS, KIND_NAMES = 0, 1
HEADER = struct.Struct("<BBHIII")  # version, kind, count, first, cycles_hz, overwritten
RECORD = struct.Struct("<IHBBIII")  # ts, id, cpu, context, a0, a1, a2
CTF_MAGIC = 0xC1FC1FC1
CONTEXTS = {0: "thread", 1: "isr", 2: "prekernel"}


class TraceError(Exception):
    pass


def catalogue(header=None):
    """{id: (name, note)} from the enum emb_trace_id in include/emb/trace.h."""
    text = Path(header or ROOT / "include" / "emb" / "trace.h").read_text(encoding="utf-8")
    body = text[text.index("enum emb_trace_id"):]
    body = body[:body.index("};")]
    out = {}
    for m in re.finditer(r"EMB_TRACE_([A-Z0-9_]+)\s*=\s*(\d+)\s*,?\s*(?:/\*(.*?)\*/)?", body, re.S):
        name, num, note = m.group(1), int(m.group(2)), " ".join((m.group(3) or "").split())
        if num:
            out[num] = (name.lower(), note)
    return out


class Event:
    __slots__ = ("seq", "cycles", "id", "cpu", "context", "a")

    def __init__(self, seq, cycles, rid, cpu, context, a):
        self.seq, self.cycles, self.id, self.cpu, self.context, self.a = seq, cycles, rid, cpu, context, a


class Trace:
    """The events of a capture, their times extended to 64 bits, and the threads' names."""

    def __init__(self):
        self.events, self.names, self.hz, self.overwritten, self.gaps = [], {}, 0, 0, 0
        self._last32 = None
        self._t64 = 0
        self._next = None

    def packet(self, payload):
        if len(payload) < HEADER.size:
            raise TraceError("a packet shorter than its header")
        version, kind, count, first, hz, overwritten = HEADER.unpack_from(payload)
        if version != 1:
            raise TraceError(f"packet version {version}: this tool reads version 1")
        self.hz = hz or self.hz
        self.overwritten = max(self.overwritten, overwritten)
        body = payload[HEADER.size:]
        if kind == KIND_NAMES:
            at = 0
            for _ in range(count):
                index, n = body[at], body[at + 1]
                self.names[index] = body[at + 2:at + 2 + n].decode(errors="replace")
                at += 2 + n
            return
        if kind != KIND_EVENTS or len(body) != count * RECORD.size:
            raise TraceError(f"a packet of kind {kind} with {len(body)} bytes for {count} events")
        if self._next is not None and first != self._next:
            self.gaps += 1  # events lost between packets: the time goes on from the counter
        for k in range(count):
            ts, rid, cpu, ctx, a0, a1, a2 = RECORD.unpack_from(body, k * RECORD.size)
            if self._last32 is None:
                self._t64 = ts
            else:
                self._t64 += (ts - self._last32) & 0xFFFFFFFF
            self._last32 = ts
            self.events.append(Event(first + k, self._t64, rid, cpu, ctx, (a0, a1, a2)))
        self._next = first + count

    def seconds(self, cycles):
        return cycles / self.hz if self.hz else 0.0


def read_capture(data):
    tr = Trace()
    dec = framing.Decoder(max_payload=4096)
    for kind, a, b in dec.feed_all(data) + dec.finish():
        if kind == "frame" and a == CH_TRACE:
            tr.packet(b)
    return tr, dec


# ------------------------------------------------------------------------------- outputs

def text(tr, cat):
    out = []
    t0 = tr.events[0].cycles if tr.events else 0
    for e in tr.events:
        name = cat.get(e.id, (f"event_{e.id}", ""))[0]
        out.append(f"{tr.seconds(e.cycles - t0) * 1e6:12.3f} us  #{e.seq:<6} {CONTEXTS.get(e.context, e.context):9} "
                   f"{name:16} {e.a[0]:#x} {e.a[1]:#x} {e.a[2]:#x}")
    return "\n".join(out) + ("\n" if out else "")


def ctf_metadata(cat, hz, trace_uuid):
    lines = [
        "/* CTF 1.8 */",
        "typealias integer { size = 8; align = 8; signed = false; } := uint8_t;",
        "typealias integer { size = 16; align = 8; signed = false; } := uint16_t;",
        "typealias integer { size = 32; align = 8; signed = false; } := uint32_t;",
        "typealias integer { size = 64; align = 8; signed = false; } := uint64_t;",
        "typealias integer { size = 64; align = 8; signed = false; map = clock.cycles.value; } := cycles_t;",
        "",
        "trace {",
        "    major = 1;",
        "    minor = 8;",
        f"    uuid = \"{trace_uuid}\";",
        "    byte_order = le;",
        "    packet.header := struct {",
        "        uint32_t magic;",
        "        uint8_t uuid[16];",
        "        uint32_t stream_id;",
        "    };",
        "};",
        "",
        "env {",
        "    domain = \"emblinkrtos\";",
        "    tracer_name = \"emblinkrtos trace ring (A128)\";",
        "};",
        "",
        "clock {",
        "    name = cycles;",
        f"    freq = {hz};",
        "    offset_s = 0;",
        "    offset = 0;",
        "    precision = 1;",
        "    absolute = false;",
        "};",
        "",
        "stream {",
        "    id = 0;",
        "    event.header := struct {",
        "        uint16_t id;",
        "        cycles_t timestamp;",
        "    };",
        "    packet.context := struct {",
        "        cycles_t timestamp_begin;",
        "        cycles_t timestamp_end;",
        "        uint64_t content_size;",
        "        uint64_t packet_size;",
        "        uint64_t events_discarded;",
        "        uint8_t cpu_id;",
        "    };",
        "};",
        "",
    ]
    for rid in sorted(cat):
        name, note = cat[rid]
        if note:
            lines.append("/* " + note.replace("*/", "* /") + " */")
        lines += [
            "event {",
            f"    name = \"{name}\";",
            f"    id = {rid};",
            "    stream_id = 0;",
            "    fields := struct {",
            "        uint8_t context;",
            "        uint32_t a0;",
            "        uint32_t a1;",
            "        uint32_t a2;",
            "    };",
            "};",
            "",
        ]
    return "\n".join(lines)


def ctf(tr, cat, out_dir, per_packet=256):
    """Writes a CTF 1.8 trace: metadata and one stream file. Events whose id the catalogue does
    not know are written as event_<id>, declared too."""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    cat = dict(cat)
    for e in tr.events:
        cat.setdefault(e.id, (f"event_{e.id}", "an event this catalogue does not name"))
    tid = uuidlib.uuid4()
    (out / "metadata").write_text(ctf_metadata(cat, tr.hz or 1, tid), encoding="utf-8")
    stream = bytearray()
    discarded = 0
    for i in range(0, len(tr.events), per_packet):
        chunk = tr.events[i:i + per_packet]
        body = bytearray()
        for e in chunk:
            body += struct.pack("<HQBIII", e.id, e.cycles, e.context, *e.a)
        head = struct.pack("<I16sI", CTF_MAGIC, tid.bytes, 0)
        size_bits = (len(head) + 8 * 5 + 1 + len(body)) * 8
        # events_discarded counts events lost up to this packet: the gaps in the numbering
        discarded = chunk[0].seq - i - (tr.events[0].seq if tr.events else 0)
        ctx = struct.pack("<QQQQQB", chunk[0].cycles, chunk[-1].cycles, size_bits, size_bits,
                          max(discarded, 0), chunk[0].cpu)
        stream += head + ctx + body
    (out / "stream_0").write_bytes(bytes(stream))
    return out


def perfetto(tr, cat):
    """Chrome's trace event JSON, which Perfetto opens: a track per thread with its running
    slices from the switches, a track for interrupts, and the other events as instants."""
    out = []
    pid = 1
    t0 = tr.events[0].cycles if tr.events else 0

    def us(c):
        return tr.seconds(c - t0) * 1e6

    for index, name in sorted(tr.names.items()):
        out.append({"ph": "M", "pid": pid, "tid": index, "name": "thread_name", "args": {"name": name}})
    out.append({"ph": "M", "pid": pid, "tid": 1000, "name": "thread_name", "args": {"name": "interrupts"}})
    running = None
    switch = next((k for k, v in cat.items() if v[0] == "switch"), 1)
    enter = next((k for k, v in cat.items() if v[0] == "irq_enter"), 10)
    leave = next((k for k, v in cat.items() if v[0] == "irq_exit"), 11)
    for e in tr.events:
        name = cat.get(e.id, (f"event_{e.id}", ""))[0]
        if e.id == switch:
            frm, to = e.a[0], e.a[1]
            if running is not None:
                out.append({"ph": "E", "pid": pid, "tid": running, "ts": us(e.cycles)})
            out.append({"ph": "B", "pid": pid, "tid": to, "ts": us(e.cycles),
                        "name": tr.names.get(to, f"thread {to}")})
            running = to
            if frm not in tr.names:
                tr.names.setdefault(frm, f"thread {frm}")
        elif e.id == enter:
            out.append({"ph": "B", "pid": pid, "tid": 1000, "ts": us(e.cycles), "name": f"irq {e.a[0]}"})
        elif e.id == leave:
            out.append({"ph": "E", "pid": pid, "tid": 1000, "ts": us(e.cycles)})
        else:
            out.append({"ph": "i", "s": "t", "pid": pid, "tid": running if running is not None else 0,
                        "ts": us(e.cycles), "name": name,
                        "args": {"a0": e.a[0], "a1": e.a[1], "a2": e.a[2], "context": CONTEXTS.get(e.context)}})
    if running is not None and tr.events:
        out.append({"ph": "E", "pid": pid, "tid": running, "ts": us(tr.events[-1].cycles)})
    return {"traceEvents": out, "displayTimeUnit": "ns",
            "otherData": {"source": "emblinkrtos trace ring (A128)", "cycles_hz": tr.hz,
                          "overwritten": tr.overwritten, "gaps": tr.gaps}}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("input", help="a capture of the framed console, or - for stdin")
    ap.add_argument("--ctf", metavar="DIR", help="write a CTF 1.8 trace into DIR")
    ap.add_argument("--perfetto", metavar="FILE", help="write Chrome/Perfetto JSON")
    ap.add_argument("--text", action="store_true", help="print one line per event")
    ap.add_argument("--header", help="the trace.h with the event catalogue (default: this tree's)")
    args = ap.parse_args(argv)
    data = sys.stdin.buffer.read() if args.input == "-" else Path(args.input).read_bytes()
    try:
        tr, dec = read_capture(data)
    except TraceError as e:
        print(f"trace: {e}", file=sys.stderr)
        return 2
    cat = catalogue(args.header)
    if not tr.events:
        print("trace: no trace packets on channel 1", file=sys.stderr)
        return 1
    if args.text or not (args.ctf or args.perfetto):
        sys.stdout.write(text(tr, cat))
    if args.ctf:
        ctf(tr, cat, args.ctf)
    if args.perfetto:
        Path(args.perfetto).write_text(json.dumps(perfetto(tr, cat), indent=0), encoding="utf-8")
    span = tr.seconds(tr.events[-1].cycles - tr.events[0].cycles)
    print(f"trace: {len(tr.events)} events over {span * 1e3:.3f} ms, {len(tr.names)} thread names, "
          f"{tr.overwritten} overwritten, {tr.gaps} gaps; {dec.errors} damaged frames", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
