### Fixed (the stack bound under LTO and Clang, 2026-11-08)

- `tools/footprint/stack_bound.py` computes the bound of an LTO image. LTO inlines `embk_isr_dispatch`, `emb_kernel_init` and the timer path into the vector entries, so their indirect calls sat in functions the roots file does not name. The tool now resolves each indirect call through the functions inlined at it, from the debug information, and matches GCC's clones (`.constprop`, `.isra`) to their originals. The reference controller's LTO bound is 216, 264 and 208 bytes for its threads and 1,076 for the interrupt stack ([page](docs/engineering/controller-memory-2026-11-07.md)).
- The tool takes a function's instructions only up to its symbol's size. Clang places string constants after the code, and one decoded as an indirect branch in the checked Clang image's `embn_cm_pendsv`.
- CI computes the controller's stack bound for a release LTO build and for the Clang build as well.
