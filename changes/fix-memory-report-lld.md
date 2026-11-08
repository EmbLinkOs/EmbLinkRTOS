### Fixed (the memory report for Clang, 2026-11-08)

- `tools/footprint/memory_report.py` reads LLD's map as well as GNU ld's, so it accounts for Clang images. LLD's map lists no memory regions; the report takes them from the linker script's `MEMORY` block, found through the link command or given with `--ldscript`. Before, it stopped with an index error on every Clang image ([page](docs/engineering/controller-memory-2026-11-07.md)). CI now reports the Clang controller's memory in the `static-analysis` job.
