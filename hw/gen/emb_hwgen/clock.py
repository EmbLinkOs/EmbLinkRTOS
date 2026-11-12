# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The clock tree (SPEC-014 §6 soc_clocks): PLL factors, bus dividers, flash wait states and
the regulator scale that reach the board's requested CPU frequency, checked against the
SoC's limits.

Factors the board gives are checked, never changed. Otherwise the search is deterministic:
M ascending (the highest VCO input, which RM0390 §6.3.2 recommends to limit jitter), then N
ascending, then P ascending; the first exact match wins. Q is the divider whose 48 MHz-domain
output is closest to 48 MHz without exceeding it, R the smallest allowed value.

A PLL with two post-dividers (`postdiv`, the RP2350's POSTDIV1 and POSTDIV2) has P their
product: every product of two values in the range is a P value, and P splits into
POSTDIV1 >= POSTDIV2 (RP2350 datasheet §8.6.3: the higher divider first saves power), POSTDIV1
the smallest such.
"""
from fractions import Fraction

HZ_48M = 48_000_000


def _in(v, rng):
    return rng[0] <= v <= rng[1]


def _mhz(hz):
    # float() first: a Fraction takes a format spec only from Python 3.12, and 3.10 is the floor
    return f"{float(Fraction(hz) / 10**6):g} MHz" if Fraction(hz).denominator == 1 else f"{float(hz) / 1e6:.3f} MHz"


def solve(soc, board, fail):
    """The derived clock record; problems go to fail(node path, message)."""
    clocks = soc.get("clocks", {})
    sources = {s["name"]: s for s in clocks.get("sources", [])}
    want = board["clocks"]["cpu_hz"]
    src_name = board["clocks"]["source"]
    out = {"cpu_hz": want, "source": src_name}

    if src_name not in sources:
        fail("clocks.source", f"the SoC has no clock source {src_name!r} (it has {', '.join(sorted(sources))})")
        return None
    src = sources[src_name]
    in_hz = src["hz"]
    if in_hz == "board":
        osc = board.get("oscillators", {}).get(src_name)
        if osc is None:
            fail("clocks.source", f"{src_name} comes from the board, which describes no oscillators.{src_name}")
            return None
        in_hz = osc["hz"]
        bypass = osc["source"] == "external_clock"
        limit = src.get("bypass_max_hz" if bypass else "max_hz")
        if limit is not None and in_hz > limit:
            fail(f"oscillators.{src_name}.hz", f"{_mhz(in_hz)} exceeds the SoC's {_mhz(limit)} for this source")
            return None
        if not bypass and "min_hz" in src and in_hz < src["min_hz"]:
            fail(f"oscillators.{src_name}.hz", f"{_mhz(in_hz)} is below the SoC's {_mhz(src['min_hz'])} crystal minimum")
            return None
        out["bypass"] = bypass
    else:
        out["bypass"] = False
    out["source_hz"] = in_hz

    pll_spec = (clocks.get("plls") or [None])[0]
    given = board["clocks"].get("pll")
    if want == in_hz and given is None:
        out["pll"] = None
        sysclk = in_hz
    else:
        if pll_spec is None:
            fail("clocks.cpu_hz", f"{_mhz(want)} differs from {src_name} ({_mhz(in_hz)}) and the SoC has no PLL")
            return None
        if src_name not in pll_spec["inputs"]:
            fail("clocks.source", f"{src_name} cannot feed {pll_spec['name']} (inputs: {', '.join(pll_spec['inputs'])})")
            return None
        pll = _check(pll_spec, in_hz, want, given, fail) if given is not None else _search(pll_spec, in_hz, want)
        if pll is None:
            if given is None:
                fail("clocks.cpu_hz", f"no {pll_spec['name']} setting reaches exactly {_mhz(want)} from {_mhz(in_hz)}")
            return None
        out["pll"] = pll
        sysclk = want

    buses = {b["name"]: b for b in clocks.get("buses", [])}
    ahb_max = min((b["max_hz"] for b in buses.values() if b["from"] == "hclk"), default=None)
    if ahb_max is not None and sysclk > ahb_max:
        fail("clocks.cpu_hz", f"{_mhz(sysclk)} exceeds the AHB limit of {_mhz(ahb_max)}")
        return None
    out["ahb_div"] = 1
    hclk = sysclk
    pres = clocks.get("prescalers", {}).get("apb", [1])
    rates = {"hclk": hclk}
    for which in ("pclk1", "pclk2"):
        limit = min((b["max_hz"] for b in buses.values() if b["from"] == which), default=None)
        if limit is None:
            continue
        div = next((d for d in sorted(pres) if hclk // d <= limit and hclk % d == 0), None)
        if div is None:
            fail("clocks.cpu_hz", f"no APB divider brings {_mhz(hclk)} under {which}'s {_mhz(limit)}")
            return None
        out[f"{which}_div"] = div
        rates[which] = hclk // div
    out["bus_hz"] = {name: rates[b["from"]] for name, b in sorted(buses.items())}
    out["hclk_hz"] = hclk
    for which in ("pclk1", "pclk2"):
        if which in rates:
            out[f"{which}_hz"] = rates[which]

    fl = clocks.get("flash_latency")
    if fl is not None:
        ws = max(0, -(-hclk // fl["hz_per_wait_state"]) - 1)
        if ws > fl["max_wait_states"]:
            fail("clocks.cpu_hz", f"{_mhz(hclk)} needs {ws} flash wait states, more than the {fl['max_wait_states']} available")
            return None
        out["flash_wait_states"] = ws

    scales = clocks.get("voltage_scales")
    if scales:
        pick = next(((s, False) for s in scales if hclk <= s["max_hz"]), None)
        if pick is None:
            pick = next(((s, True) for s in scales if hclk <= s.get("overdrive_max_hz", 0)), None)
        if pick is None:
            fail("clocks.cpu_hz", f"{_mhz(hclk)} exceeds every regulator scale, over-drive included")
            return None
        out["voltage_scale"] = {"name": pick[0]["name"], "vos": pick[0]["vos"], "overdrive": pick[1]}
    return out


def _p_values(spec):
    """The P values a PLL allows: its own list, or the products of its two post-dividers."""
    if "postdiv" in spec:
        lo, hi = spec["postdiv"]
        return sorted({a * b for a in range(lo, hi + 1) for b in range(lo, hi + 1)})
    return sorted(spec["p"])


def _postdivs(spec, p):
    """P as (POSTDIV1, POSTDIV2), POSTDIV1 >= POSTDIV2 and the smallest such."""
    lo, hi = spec["postdiv"]
    return min((a, p // a) for a in range(lo, hi + 1) if p % a == 0 and lo <= p // a <= hi and a >= p // a)


def _q_r(spec, vco):
    out = {}
    if "q" in spec:
        qs = [q for q in range(spec["q"][0], spec["q"][1] + 1)]
        under = [q for q in qs if vco / q <= HZ_48M]
        out["q"] = min(under, key=lambda q: (HZ_48M - vco / q, q)) if under else qs[-1]
    if "r" in spec:
        out["r"] = spec["r"][0]
    return out


def _record(spec, in_hz, m, n, p, extra):
    vco = Fraction(in_hz, m) * n
    rec = {"name": spec["name"], "m": m, "n": n, "p": p}
    if "postdiv" in spec:
        rec["postdiv1"], rec["postdiv2"] = _postdivs(spec, p)
    rec.update(extra)
    rec["vco_in_hz"] = int(Fraction(in_hz, m)) if Fraction(in_hz, m).denominator == 1 else float(Fraction(in_hz, m))
    rec["vco_hz"] = int(vco) if vco.denominator == 1 else float(vco)
    if "q" in rec:
        rec["q_hz"] = int(vco / rec["q"])
    return rec


def _search(spec, in_hz, want):
    for m in range(spec["m"][0], spec["m"][1] + 1):
        vin = Fraction(in_hz, m)
        if not _in(vin, spec["vco_in_hz"]):
            continue
        for n in range(spec["n"][0], spec["n"][1] + 1):
            vco = vin * n
            if not _in(vco, spec["vco_out_hz"]):
                continue
            for p in _p_values(spec):
                if vco / p == want:
                    return _record(spec, in_hz, m, n, p, _q_r(spec, vco))
    return None


def _check(spec, in_hz, want, given, fail):
    ok = True
    for f in ("m", "n", "q", "r"):
        if f in given:
            if f not in spec:
                fail(f"clocks.pll.{f}", f"{spec['name']} has no {f} divider")
                ok = False
            elif not _in(given[f], spec[f]):
                fail(f"clocks.pll.{f}", f"{given[f]} is outside {spec['name']}'s {f} range {spec[f][0]} to {spec[f][1]}")
                ok = False
    if given["p"] not in _p_values(spec):
        what = (f"a product of two post-dividers {spec['postdiv'][0]} to {spec['postdiv'][1]}"
                if "postdiv" in spec else f"one of {spec['name']}'s P values {spec['p']}")
        fail("clocks.pll.p", f"{given['p']} is not {what}")
        ok = False
    if not ok:
        return None
    m, n, p = given["m"], given["n"], given["p"]
    vin = Fraction(in_hz, m)
    vco = vin * n
    if not _in(vin, spec["vco_in_hz"]):
        fail("clocks.pll.m", f"VCO input {_mhz(vin)} is outside {_mhz(spec['vco_in_hz'][0])} to {_mhz(spec['vco_in_hz'][1])}")
        return None
    if not _in(vco, spec["vco_out_hz"]):
        fail("clocks.pll.n", f"VCO output {_mhz(vco)} is outside {_mhz(spec['vco_out_hz'][0])} to {_mhz(spec['vco_out_hz'][1])}")
        return None
    if vco / p != want:
        fail("clocks.pll", f"these factors give {_mhz(vco / p)}, not the requested {_mhz(want)}")
        return None
    extra = _q_r(spec, vco)
    extra.update({k: given[k] for k in ("q", "r") if k in given})
    return _record(spec, in_hz, m, n, p, extra)
