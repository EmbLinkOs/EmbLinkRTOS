# Software bill of materials

REL-001 requires every release to come with an SBOM. BLD-003 requires a build manifest: the source revision, the configuration hash, the toolchain's identity and the hardware description's hash. `tools/sbom/sbom.py` writes both for one image, as an [SPDX 2.3](https://spdx.github.io/spdx-spec/v2.3/) JSON document.

```sh
python3 tools/sbom/sbom.py --build build/controller --elf build/controller/controller.elf
# writes build/controller/controller.spdx.json
```

## What it lists

The tool reads only what the build already recorded. It runs nothing but `ninja -t deps` and the compiler's `--version`.

| Package | Contents | Licence |
|---|---|---|
| **the image** (`FIRMWARE`) | its SHA-256, and the build manifest in its comment | — |
| **EmbLinkRTOS** | every project file compiled into the image: the sources of the linked objects, from `compile_commands.json`, and every header they included, from Ninja's dependency log. Generated files are listed as `./build/...`. Each file has its SHA-1, SHA-256 and the licence of its `SPDX-License-Identifier` line. | declared Apache-2.0; per file, from the files |
| **the application**, when it lies outside the RTOS's tree | its files, as above | from its files |
| **each toolchain library linked** (`libc_nano.a`, `libgcc.a`, picolibc's `libc.a`, avr-libc's start-up object) | the archive's SHA-256, and in its comment the members the linker took | `libgcc.a` is declared `GPL-3.0-or-later WITH GCC-exception-3.1`; the others `NOASSERTION` |
| **the compiler** | a `BUILD_TOOL_OF` the image, with its `--version` line | `NOASSERTION` |

**What was linked** comes from the link map. The tool reads both GNU ld's and LLD's formats. A project object inside a static library (`libemb_kernel.a(sched.c.obj)`) is traced back to its source through the library's CMake target.

**The build manifest (BLD-003)** sits in the image package's comment:
- `source_revision`: the commit, with `+dirty` when tracked files differed from it;
- `configuration_hash`: from `generated/config.json`;
- `toolchain`: the compiler's identity and version;
- `hardware_description_sha256`: over the board, SoC and architecture descriptions the board generator recorded as its inputs, with their paths;
- `build_id`: the 20-byte identity the image itself carries (A72).

The commit, the configuration hash and the toolchain (the compiler's identity and version, and whether LTO is on) are embedded in the image through the build identity, which is a digest of them (A72). The hardware description's hash is not part of the build identity; it is in the SBOM only.

**The document is deterministic.** Its creation time is the commit's, its namespace derives from the image's hash, and every list is sorted. The same build gives the same bytes. It does not affect the double build of BLD-004, which compares images, not SBOMs.

## Checked by

- **CI:** the `application` job builds the reference controller for both NUCLEO-F446 boards out of the tree and writes their SBOMs. `pyspdxtools` from `spdx-tools` 0.8.5, the SPDX project's own validator, then checks them. The job keeps them as the `sbom-controller` artifact.
- **Unit tests:** `tools/sbom/test_sbom.py` checks the readers of GNU ld and LLD maps, the build identity, the hardware hash and licence lines. CI runs it in lint.
- **By hand on 2026-11-08,** validated by `pyspdxtools`:

  | Image | Files listed | Toolchain libraries |
  |---|---|---|
  | controller, GCC and Clang (LLD) | 76 | `libc_nano.a`, `libgcc.a` |
  | AN386 Clang test image | 70 | `libc_nano.a`, `libgcc.a` |
  | RV32 test image | 69 | picolibc's `libc.a`, `libgcc.a` |
  | AVR test image | 57 | avr-libc's `libc.a`, `crtatmega328p.o`, `libgcc.a` |
  | out-of-tree `hello_app` | 67, with its own application package | `libc_nano.a`, `libgcc.a` |

  Every project file in them declares Apache-2.0. The generated build identity source (`build_id.c`) now carries the SPDX line too.

## Limits

- **The toolchain's headers are not listed file by file**, and the toolchain libraries' licences are not concluded. newlib, picolibc and avr-libc each combine several licences. A release's licence notices take them from the toolchain's own documentation, not from this file.
- **The build manifest is in a comment.** SPDX 2.3 has no field for a configuration hash or a hardware description. Tools that read only standard fields see the image's checksum and the source package's commit, from its `downloadLocation`.
- **Native programs on macOS** link with Apple's ld64, whose map format the tool does not read. Native programs on Linux link with GNU ld, whose maps it does read, but no SBOM of one has been checked.
- **The build does not write the SBOM itself.** `ninja -t deps` reads Ninja's dependency log, which a running Ninja is still writing. Run the tool after the build, as CI and a release do.
- **SPDX 2.3 JSON only:** no SPDX 3 and no CycloneDX.
