#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Measure the reference controller's timing from outside, with a logic analyzer (M3a).

    controller_timing.py --port /dev/cu.usbmodem11203 --out <record directory> \\
                         [--samplerate 12m] [--seconds 20] [--scenario normal ...]

examples/controller drives marker pins: TIM2 toggles PA0 at each sampling trigger in hardware,
PA10 is high during the ADC's handler, PA8 while the control thread processes a sample, PA9
while a thread holds the console. A sigrok-supported analyzer (sigrok-cli) records them while
this tool sets the controller's rate and load through its console. For every sample it measures:

- conversion: the trigger's edge to the handler's start (the ADC's conversion, then the
  interrupt's entry);
- handler: the handler's length;
- wake: the handler's end to the control thread's marker (the kernel's switch to the woken
  thread and its return from the receive);
- latency: the handler's start to the control thread's marker, what the controller's own
  lat_max_us measures with the cycle counter;
- period: between consecutive triggers, and between consecutive control starts.

Each scenario keeps its capture (VCD, gzipped), and the run writes timing.json (every
scenario's statistics) and SVG images: one sample's chain, and the wake-latency histograms.
Exit status 0 when every scenario was captured and measured, 1 otherwise, 2 on a usage error.
"""
import argparse
import gzip
import json
import math
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import controller_session  # noqa: E402  (Console)
import run_hil  # noqa: E402  (git_state)

FORMAT = "emb-controller-timing v1"
CHANNELS = {"trigger": "D1", "isr": "D2", "control": "D3", "console": "D4", "led": "D5"}
SCENARIOS = {
    "normal": {"rate": 100, "load": 0, "doc": "100 Hz, no added load"},
    "load30": {"rate": 100, "load": 30, "doc": "100 Hz, 3 ms of work per sample (the soak's load)"},
    "fast": {"rate": 1000, "load": 50, "doc": "1 kHz, 0.5 ms of work per sample"},
    "overload": {"rate": 100, "load": 150, "doc": "100 Hz, 15 ms of work per 10 ms period: degraded mode"},
}


# ---- the capture ----------------------------------------------------------------------------

def load_vcd(text):
    """{channel name: [(time ns, level), ...]} and the initial levels, from sigrok's VCD."""
    ids, scale, edges, init = {}, 1.0, {}, {}
    units = {"ps": 0.001, "ns": 1.0, "us": 1000.0, "ms": 1e6}
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r"\$timescale\s+(\d+)\s*(\w+)", line)
        if m:
            scale = int(m.group(1)) * units[m.group(2)]
            continue
        m = re.match(r"\$var wire 1 (\S+) (\S+) \$end", line)
        if m:
            ids[m.group(1)] = m.group(2)
            edges[m.group(2)] = []
            continue
        if line.startswith("#"):
            parts = line.split()
            t = int(parts[0][1:]) * scale
            for p in parts[1:]:
                level, ch = int(p[0]), ids.get(p[1:])
                if ch is None:
                    continue
                if t == 0:
                    init[ch] = level
                else:
                    edges[ch].append((t, level))
    return edges, init


def rises(edges):
    return [t for t, v in edges if v == 1]


def pulses(edges):
    """[(rise, fall)] for each complete high pulse."""
    out, start = [], None
    for t, v in edges:
        if v == 1:
            start = t
        elif start is not None:
            out.append((start, t))
            start = None
    return out


def level_at(edges, init, t):
    level = init
    for et, v in edges:
        if et > t:
            break
        level = v
    return level


def measure(edges, init, ch=CHANNELS):
    """Per-sample intervals in ns, matched trigger -> handler -> control."""
    trig = [t for t, _ in edges[ch["trigger"]]]  # both edges: the pin toggles once per trigger
    isr = pulses(edges[ch["isr"]])
    ctl = pulses(edges[ch["control"]])
    con = edges.get(ch["console"], [])
    samples, ti, ci = [], 0, 0
    for k, (i_rise, i_fall) in enumerate(isr):
        nxt = isr[k + 1][0] if k + 1 < len(isr) else math.inf
        while ti + 1 < len(trig) and trig[ti + 1] <= i_rise:
            ti += 1
        conv = (i_rise - trig[ti]) if trig and trig[ti] <= i_rise else None
        while ci < len(ctl) and ctl[ci][0] < i_fall:
            ci += 1
        c = ctl[ci] if ci < len(ctl) and ctl[ci][0] < nxt else None
        s = {"isr_rise": i_rise, "conversion": conv, "handler": i_fall - i_rise,
             "console": level_at(con, init.get(ch["console"], 0), i_rise)}
        if c is not None:
            s.update({"control_rise": c[0], "wake": c[0] - i_fall, "latency": c[0] - i_rise,
                      "processing": c[1] - c[0]})
        samples.append(s)
    return samples, trig, ctl


def stats(values):
    v = sorted(x for x in values if x is not None)
    if not v:
        return None
    def pct(p):
        return v[min(len(v) - 1, int(round(p / 100 * (len(v) - 1))))]
    return {"n": len(v), "min": round(v[0], 1), "median": round(statistics.median(v), 1),
            "p99": round(pct(99), 1), "max": round(v[-1], 1),
            "stdev": round(statistics.pstdev(v), 1) if len(v) > 1 else 0.0}


def summarize(samples, trig, ctl):
    periods = [b - a for a, b in zip(trig, trig[1:])]
    starts = [s["control_rise"] for s in samples if "control_rise" in s]
    out = {
        "samples": len(samples),
        "processed": len(starts),
        "unprocessed": len(samples) - len(starts),
        "conversion_ns": stats(s["conversion"] for s in samples),
        "handler_ns": stats(s["handler"] for s in samples),
        "wake_ns": stats(s.get("wake") for s in samples),
        "latency_ns": stats(s.get("latency") for s in samples),
        "processing_ns": stats(s.get("processing") for s in samples),
        "trigger_period_ns": stats(periods),
        "control_period_ns": stats(b - a for a, b in zip(starts, starts[1:])),
        "latency_console_busy_ns": stats(s.get("latency") for s in samples if s["console"]),
        "latency_console_idle_ns": stats(s.get("latency") for s in samples if not s["console"]),
    }
    return out


def capture(seconds, samplerate, channels, path):
    """Record the channels with sigrok-cli into path (VCD); retried while the analyzer reloads."""
    # every channel: libsigrok 0.5.2's VCD output mislabels a subset chosen with -C, and the
    # analyzer transfers all eight anyway
    del channels
    cmd = ["sigrok-cli", "-d", "fx2lafw", "--config", f"samplerate={samplerate}",
           "--time", str(int(seconds * 1000)), "-O", "vcd", "-o", str(path)]
    for _ in range(5):
        if path.exists():
            path.unlink()
        done = subprocess.run(cmd, capture_output=True, text=True, timeout=seconds + 60)
        if path.exists() and path.stat().st_size > 0:
            return None
        time.sleep(4)
    return f"sigrok-cli captured nothing: {done.stderr.strip()[-300:]}"


# ---- the images (SVG, no dependency) --------------------------------------------------------

STYLE = ("<style>text{font:12px sans-serif;fill:#222}.s{stroke:#1f6feb;stroke-width:1.5;fill:none}"
         ".a{stroke:#888;stroke-width:1;stroke-dasharray:3 3}.b{fill:#1f6feb;opacity:.75}"
         ".g{stroke:#ddd;stroke-width:1}.t{font-weight:bold}</style>")


def svg_chain(edges, init, sample, title, ch=CHANNELS, before=5000, after=45000):
    """One sample's chain: each marker's level from before the trigger to after the processing."""
    t0 = sample["isr_rise"] - (sample["conversion"] or 0)
    t1 = max(t0 + after, sample.get("control_rise", t0) + min(sample.get("processing", 0), 20000) + 5000)
    lo, hi = t0 - before, t1
    w, left, row = 860, 170, 46
    names = [("trigger", "PA0  TIM2 trigger"), ("isr", "PA10 ADC handler"),
             ("control", "PA8  control thread"), ("console", "PA9  console held")]
    h = 60 + row * len(names) + 22 * 4 + 40
    x = lambda t: left + (t - lo) / (hi - lo) * (w - left - 20)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">', STYLE,
           '<defs><marker id="m" viewBox="0 0 2 10" refX="1" refY="5" markerWidth="2" markerHeight="10">'
           '<rect width="2" height="10" fill="#1f6feb"/></marker></defs>',
           f'<rect width="{w}" height="{h}" fill="#fff"/>', f'<text class="t" x="10" y="20">{title}</text>']
    for r, (key, label) in enumerate(names):
        y0 = 50 + r * row
        hy, ly = y0 + 6, y0 + 30
        level = level_at(edges[ch[key]], init.get(ch[key], 0), lo)
        pts, cur = [(x(lo), hy if level else ly)], level
        for t, v in edges[ch[key]]:
            if lo < t < hi and v != cur:
                pts += [(x(t), hy if cur else ly), (x(t), hy if v else ly)]
                cur = v
        pts.append((x(hi), hy if cur else ly))
        out.append(f'<text x="10" y="{y0 + 23}">{label}</text>')
        out.append('<polyline class="s" points="' + " ".join(f"{a:.1f},{b:.1f}" for a, b in pts) + '"/>')
    ybase = 50 + row * len(names)
    spans = [(t0, sample["isr_rise"], f"conversion {sample['conversion'] / 1000:.2f} µs"),
             (sample["isr_rise"], sample["isr_rise"] + sample["handler"], f"handler {sample['handler'] / 1000:.2f} µs")]
    if "control_rise" in sample:
        spans.append((sample["isr_rise"] + sample["handler"], sample["control_rise"],
                      f"wake {sample['wake'] / 1000:.2f} µs"))
        spans.append((sample["isr_rise"], sample["control_rise"], f"latency {sample['latency'] / 1000:.2f} µs"))
    for t in sorted({a for a, _, _ in spans} | {b for _, b, _ in spans}):
        out.append(f'<line class="a" x1="{x(t):.1f}" y1="40" x2="{x(t):.1f}" y2="{ybase + 8 + 22 * len(spans)}"/>')
    for i, (a, b, label) in enumerate(spans):
        y = ybase + 14 + 22 * i
        out.append(f'<line class="s" x1="{x(a):.1f}" y1="{y}" x2="{x(b):.1f}" y2="{y}" '
                   f'marker-start="url(#m)" marker-end="url(#m)"/>')
        anchor, tx = ("end", x(a) - 6) if x(b) + 160 > w else ("start", x(b) + 6)
        out.append(f'<text x="{tx:.1f}" y="{y + 4}" text-anchor="{anchor}">{label}</text>')
    for us in range(0, int((hi - lo) / 1000) + 1, 5):
        tx = x(lo + us * 1000)
        out.append(f'<line class="g" x1="{tx:.1f}" y1="{h - 22}" x2="{tx:.1f}" y2="{h - 18}"/>')
        out.append(f'<text x="{tx - 8:.1f}" y="{h - 6}" style="fill:#888">{us - before // 1000} µs</text>')
    out.append("</svg>")
    return "\n".join(out) + "\n"


def svg_histograms(series, title, key="wake", width_ns=1e9 / 12e6):
    """One histogram per scenario of a per-sample interval, on a common axis."""
    vals = {n: sorted(s[key] for s in smp if s.get(key) is not None) for n, smp in series}
    allv = [v for vs in vals.values() for v in vs]
    if not allv:
        return None
    lo = math.floor(min(allv) / width_ns) * width_ns
    hi = math.ceil(max(allv) / width_ns + 1) * width_ns
    nb = max(1, int((hi - lo) / width_ns))
    w, left, ph = 860, 170, 90
    h = 50 + ph * len(vals) + 30
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">', STYLE,
           f'<rect width="{w}" height="{h}" fill="#fff"/>', f'<text class="t" x="10" y="20">{title}</text>']
    bw = (w - left - 20) / nb
    for r, (name, vs) in enumerate(vals.items()):
        y0 = 40 + r * ph
        counts = [0] * nb
        for v in vs:
            counts[min(nb - 1, int((v - lo) / width_ns))] += 1
        peak = max(counts) or 1
        out.append(f'<line class="g" x1="{left}" y1="{y0 + ph - 15}" x2="{w - 20}" y2="{y0 + ph - 15}"/>')
        out.append(f'<text x="10" y="{y0 + 30}">{name}</text>')
        if vs:
            out.append(f'<text x="10" y="{y0 + 46}" style="fill:#666">n={len(vs)} '
                       f'max {vs[-1] / 1000:.2f} µs</text>')
        for b, c in enumerate(counts):
            if c:
                bh = max(1.5, (ph - 25) * math.log1p(c) / math.log1p(peak))
                out.append(f'<rect class="b" x="{left + b * bw:.1f}" y="{y0 + ph - 15 - bh:.1f}" '
                           f'width="{max(1.0, bw - 1):.1f}" height="{bh:.1f}"/>')
    step = max(1, nb // 8)
    for b in range(0, nb + 1, step):
        out.append(f'<text x="{left + b * bw - 10:.1f}" y="{h - 8}" style="fill:#888">'
                   f'{(lo + b * width_ns) / 1000:.2f} µs</text>')
    out.append(f'<text x="{w - 260}" y="{h - 24}" style="fill:#888">bar height: log of the count</text>')
    out.append("</svg>")
    return "\n".join(out) + "\n"


# ---- the run --------------------------------------------------------------------------------

def rate_hz(text):
    """sigrok's samplerate spelling (12m, 500k, 24000000) in Hz."""
    m = re.fullmatch(r"(\d+(?:\.\d+)?)\s*([kKmM]?)(?:hz|Hz)?", text.strip())
    if not m:
        raise ValueError(f"samplerate {text!r}")
    return float(m.group(1)) * {"": 1, "k": 1e3, "K": 1e3, "m": 1e6, "M": 1e6}[m.group(2)]


def capture_scenario(con, name, sc, args, out_dir):
    """Set the scenario through the console, capture it, keep the capture gzipped."""
    replies = [con.command(f"rate {sc['rate']}"), con.command(f"load {sc['load']}")]
    con.pump(args.settle)
    vcd = out_dir / f"{name}.vcd"
    problem = capture(args.seconds, args.samplerate, list(CHANNELS.values()), vcd)
    con.pump(0.5)
    if problem:
        return {"name": name, "replies": replies, "problem": problem}
    with open(vcd, "rb") as src, gzip.open(out_dir / f"{name}.vcd.gz", "wb") as dst:
        dst.write(src.read())
    vcd.unlink()
    return {"name": name, "replies": replies}


def analyze(name, out_dir):
    """A scenario's statistics from its kept capture, and what the images need."""
    sc = SCENARIOS[name]
    with gzip.open(out_dir / f"{name}.vcd.gz", "rt", encoding="utf-8") as f:
        edges, init = load_vcd(f.read())
    samples, trig, ctl = measure(edges, init)
    result = {"name": name, "doc": sc["doc"], "rate_hz": sc["rate"], "load_pct": sc["load"],
              "capture": f"{name}.vcd.gz", **summarize(samples, trig, ctl)}
    if not samples:
        result["problem"] = "no ADC handler pulse in the capture"
    return result, (edges, init, samples)


def images(data, out_dir, bin_ns):
    for name, (edges, init, samples) in data.items():
        ok = [s for s in samples if "latency" in s and s["conversion"] is not None]
        if ok:
            med = sorted(ok, key=lambda s: s["latency"])[len(ok) // 2]
            title = f"One sample, {SCENARIOS[name]['doc']}: the median latency"
            (out_dir / f"chain-{name}.svg").write_text(svg_chain(edges, init, med, title), encoding="utf-8")
    for key, title in (("wake", "The handler's end to the control thread at work (wake), per sample"),
                       ("latency", "The handler's start to the control thread at work (latency), per sample")):
        svg = svg_histograms([(n, d[2]) for n, d in data.items()], title, key, bin_ns)
        if svg:
            (out_dir / f"{key}-histogram.svg").write_text(svg, encoding="utf-8")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", help="the controller's console (not with --from)")
    ap.add_argument("--out", required=True, help="the record directory")
    ap.add_argument("--from", dest="from_captures", action="store_true",
                    help="recompute timing.json and the images from the captures already in --out")
    ap.add_argument("--samplerate", default="12m", help="sigrok's samplerate (default 12m)")
    ap.add_argument("--seconds", type=float, default=20.0, help="each capture's length (default 20)")
    ap.add_argument("--settle", type=float, default=4.0, help="seconds between a change and its capture")
    ap.add_argument("--scenario", action="append", choices=sorted(SCENARIOS),
                    help="run these (default: all, in order)")
    args = ap.parse_args(argv)
    if not args.from_captures and not args.port:
        ap.error("--port, or --from to reuse the captures")
    try:
        bin_ns = 1e9 / rate_hz(args.samplerate)
    except ValueError as e:
        ap.error(str(e))
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    names = args.scenario or list(SCENARIOS)
    rec_path = out_dir / "timing.json"
    previous = json.loads(rec_path.read_text(encoding="utf-8")) if args.from_captures and rec_path.is_file() else {}
    captured = {}
    if args.from_captures:
        names = [n for n in names if (out_dir / f"{n}.vcd.gz").is_file()]
        captured = {r["name"]: r for r in previous.get("scenarios", [])}
    else:
        con = controller_session.Console(args.port)
        started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        try:
            con.pump(1.5)
            for name in names:
                print(f"\n=== {name}: {SCENARIOS[name]['doc']}", flush=True)
                captured[name] = capture_scenario(con, name, SCENARIOS[name], args, out_dir)
            con.command("rate 100")
            con.command("load 0")
        finally:
            os.close(con.fd)
        (out_dir / "console.log").write_text(con.log, encoding="utf-8")
    results, data = [], {}
    for name in names:
        res = dict(captured.get(name, {"name": name}))
        if "problem" not in res or args.from_captures:
            res.pop("problem", None)
            measured, d = analyze(name, out_dir)
            res.update(measured)
            data[name] = d
            lat = res.get("latency_ns") or {}
            print(f"=== {name}: {res['samples']} samples, latency median {lat.get('median')} ns, "
                  f"max {lat.get('max')} ns", flush=True)
        results.append(res)
    images(data, out_dir, bin_ns)
    ok = bool(results) and all("problem" not in r for r in results)
    rec = previous or {"format": FORMAT, "board": "nucleo_f446re", "port": args.port,
                       "analyzer": "fx2lafw (sigrok-cli)", "samplerate": args.samplerate,
                       "seconds": args.seconds, "channels": CHANNELS, "git": run_hil.git_state(),
                       "started": started, "ended": time.strftime("%Y-%m-%dT%H:%M:%S%z")}
    rec.update({"outcome": "pass" if ok else "fail", "scenarios": results})
    rec_path.write_text(json.dumps(rec, indent=2) + "\n", encoding="utf-8")
    print(f"\ncontroller_timing: {len(results)} scenario(s), {'all measured' if ok else 'PROBLEMS'} -> {out_dir}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
