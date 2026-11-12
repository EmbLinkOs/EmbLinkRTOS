### Fixed (the generator on Python 3.10 and 3.11, 2026-11-12)

- `emb-hwgen` no longer crashes with `TypeError: unsupported format string passed to Fraction.__format__` when it reports a clock error on Python 3.10 or 3.11: its frequencies are formatted as floats, since a `Fraction` takes a format specification only from Python 3.12. Reported by the EmbLink Studio tests.
- CI's `python-floor` job runs every host tool's tests, the reference model, the generator's drift check and a native build on Python 3.10, the floor the README states. The lint job keeps running them on 3.12.
- CMake asks for Python 3.10 or newer, as the README and getting-started guide state, instead of 3.9.
