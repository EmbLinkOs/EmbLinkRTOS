#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Draws every EmbLinkRTOS logo asset from one definition of the mark.

    python3 .github/assets/brand.py          # rewrite the SVGs
    rsvg-convert is then used for the PNGs (see PNGS below)

The mark is the "trace E": the letter E drawn as one circuit-board trace, its three arms ending
in solder pads. The spine sits left of centre because the pads overhang the arms. Colours are
the brand's four; everything else derives from them.
"""
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".github/assets"
STATIC = ROOT / "docs/_static"

INK, TEAL, TEAL_DARK, MINT, PAPER = "#0b1220", "#0f766e", "#115e59", "#5eead4", "#f8fafc"
FONT = "Inter, Segoe UI, Helvetica Neue, Arial, sans-serif"
MONO = "ui-monospace, SFMono-Regular, Menlo, Consolas, monospace"

# the mark's geometry on a 200 x 200 canvas
SPINE_X, TOP, BOTTOM, ARM_X, MID_X = 50, 46, 154, 143, 115
STROKE, PAD_R, HOLE_R = 24, 18, 6.5
PADS = ((ARM_X, TOP), (MID_X, (TOP + BOTTOM) / 2), (ARM_X, BOTTOM))


def mark(trace, pad, hole=None):
    """The mark's elements. Without a hole colour the drill holes are transparent: each pad is a
    ring, so the mark works on any background."""
    mid_y = (TOP + BOTTOM) / 2
    if hole:  # opaque pads cover the arms' ends
        s = (f'<path d="M{ARM_X} {TOP}H{SPINE_X}V{BOTTOM}H{ARM_X}M{SPINE_X} {mid_y}H{MID_X}" fill="none" '
             f'stroke="{trace}" stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round"/>')
    else:  # through a transparent hole the trace would show: the arms stop inside each ring
        stop = PAD_R - 3
        s = (f'<path d="M{ARM_X - stop} {TOP}H{SPINE_X}V{BOTTOM}H{ARM_X - stop}" fill="none" stroke="{trace}" '
             f'stroke-width="{STROKE}" stroke-linejoin="round"/>'
             f'<path d="M{SPINE_X} {mid_y}H{MID_X - stop}" stroke="{trace}" stroke-width="{STROKE}"/>')
    for x, y in PADS:
        if hole:
            s += f'<circle cx="{x}" cy="{y}" r="{PAD_R}" fill="{pad}"/><circle cx="{x}" cy="{y}" r="{HOLE_R}" fill="{hole}"/>'
        else:
            ring = (PAD_R + HOLE_R) / 2
            s += f'<circle cx="{x}" cy="{y}" r="{ring}" fill="none" stroke="{pad}" stroke-width="{PAD_R - HOLE_R}"/>'
    return s


def tile(bg, trace, pad, hole, rx=46):
    return f'<rect width="200" height="200" rx="{rx}" fill="{bg}"/>' + mark(trace, pad, hole)


def svg(w, h, body, view=None):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="{view or f"0 0 {w} {h}"}">\n'
            f'  <!-- SPDX-License-Identifier: Apache-2.0 -->\n'
            f'  <!-- Drawn by .github/assets/brand.py; edit the script, not this file -->\n'
            f'  {body}\n</svg>\n')


BRAND_TILE = tile(TEAL, PAPER, MINT, TEAL)   # the logo: white trace, mint pads on teal
DARK_TILE = tile(INK, MINT, PAPER, INK)      # on dark surfaces: avatar, banner


def wordmark(x, y, size, ink, accent):
    return (f'<text x="{x}" y="{y}" fill="{ink}" font-family="{FONT}" font-size="{size}" font-weight="800" '
            f'letter-spacing="{-size * 0.025:.1f}">EmbLink<tspan fill="{accent}">RTOS</tspan></text>')


def chips(x, y, names, size, height):
    out, cx = [], x
    for n in names:
        w = int(len(n) * size * 0.62 + size * 1.8)
        out.append(f'<g transform="translate({cx} {y})"><rect width="{w}" height="{height}" rx="{height / 2}" '
                   f'fill="#1e293b" stroke="#334155"/><text x="{w / 2}" y="{height * 0.67}" text-anchor="middle" '
                   f'font-family="{MONO}" font-size="{size}" fill="#e2e8f0">{n}</text></g>')
        cx += w + 12
    return "".join(out)


BACKDROP = ('<defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#0b1220"/>'
            '<stop offset="1" stop-color="#13233f"/></linearGradient><pattern id="grid" width="32" height="32" '
            'patternUnits="userSpaceOnUse"><path d="M32 0H0V32" fill="none" stroke="#ffffff" stroke-opacity="0.045"/>'
            '</pattern></defs>')


def banner():
    body = (BACKDROP + '<rect width="1280" height="320" rx="18" fill="url(#bg)"/>'
            '<rect width="1280" height="320" rx="18" fill="url(#grid)"/>'
            f'<g transform="translate(88 70) scale(0.9)">{BRAND_TILE}</g>'
            + wordmark(300, 150, 76, PAPER, MINT)
            + f'<text x="302" y="198" fill="#cbd5e1" font-family="{FONT}" font-size="28">'
              'One deterministic kernel, from 8-bit AVR to 32-bit Arm, RISC-V and Xtensa</text>'
            + chips(302, 228, ["AVR", "Cortex-M0 … M33", "RISC-V", "Xtensa", "native"], 19, 36))
    return svg(1280, 320, body)


def social():
    lines = ["Runs on real boards: Arduino Uno, STM32 NUCLEO, Raspberry Pi Pico 2 W, ESP32",
             "Requirements traced to executed tests, timing numbers to their runs",
             "GCC, Clang and EmbCC  ·  MISRA-oriented C  ·  reproducible builds  ·  Apache-2.0"]
    text = "".join(f'<text x="110" y="{476 + 46 * i}" font-family="{FONT}" font-size="27" fill="#e2e8f0">'
                   f'<tspan fill="{MINT}">●</tspan>  {t}</text>' for i, t in enumerate(lines))
    body = (BACKDROP + '<rect width="1280" height="640" fill="url(#bg)"/><rect width="1280" height="640" fill="url(#grid)"/>'
            f'<g transform="translate(100 116) scale(1.1)">{BRAND_TILE}</g>'
            + wordmark(372, 226, 92, PAPER, MINT)
            + f'<text x="376" y="284" fill="#cbd5e1" font-family="{FONT}" font-size="32">'
              'A deterministic real-time kernel for microcontrollers</text>'
            + chips(376, 322, ["AVR", "Cortex-M0 … M33", "RISC-V", "Xtensa"], 22, 42) + text)
    return svg(1280, 640, body)


def lockup(ink, accent, tile_body):
    return svg(900, 200, f'<g>{tile_body}</g>' + wordmark(232, 132, 92, ink, accent))


FILES = {
    ASSETS / "logo.svg": svg(512, 512, BRAND_TILE, "0 0 200 200"),
    ASSETS / "logo-mono.svg": svg(512, 512, mark(INK, INK), "0 0 200 200"),
    ASSETS / "avatar.svg": svg(512, 512, f'<rect width="200" height="200" fill="{INK}"/>'
                                         f'<g transform="translate(100 100) scale(0.8) translate(-100 -100)">{BRAND_TILE}</g>',
                               "0 0 200 200"),
    ASSETS / "lockup-light.svg": lockup(INK, TEAL, BRAND_TILE),
    ASSETS / "lockup-dark.svg": lockup(PAPER, MINT, BRAND_TILE),
    ASSETS / "banner.svg": banner(),
    ASSETS / "social-preview.svg": social(),
    STATIC / "logo-light.svg": svg(160, 160, mark(TEAL, INK), "0 0 200 200"),
    STATIC / "logo-dark.svg": svg(160, 160, mark(MINT, PAPER), "0 0 200 200"),
    STATIC / "favicon.svg": svg(32, 32, BRAND_TILE, "0 0 200 200"),
}
PNGS = {  # for places that take no SVG: GitHub's avatar and social preview
    ASSETS / "avatar.png": (ASSETS / "avatar.svg", 512, 512),
    ASSETS / "social-preview.png": (ASSETS / "social-preview.svg", 1280, 640),
}


def main():
    for path, text in FILES.items():
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path.relative_to(ROOT)}")
    tool = shutil.which("rsvg-convert")
    if not tool:
        print("rsvg-convert not found: the PNGs are not redrawn")
        return
    for png, (src, w, h) in PNGS.items():
        subprocess.run([tool, "-w", str(w), "-h", str(h), str(src), "-o", str(png)], check=True)
        print(f"wrote {png.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
