### Added (the generator's output for tools, 2026-11-13)

- `emb-hwgen validate`, `generate` and `check` take `--format json`, which prints one JSON document for an IDE or a script (`hw/schemas/diagnostics.schema.json`). It gives the status and exit code, every error and warning with its file, line, node path and message as separate fields, the diff of each outdated generated file, and the files written ([hwgen](docs/engineering/hwgen.md)). A failure of the generator itself still yields a document, with exit status 3. EmbLink Studio's first stage reads it.
