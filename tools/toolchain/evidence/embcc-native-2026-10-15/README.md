# Linux native EmbCC kernel evidence

Execution source: `df32ec21557aeb0cd12e66cbc756c94e1cef5d17` (clean tracked tree during capture). Compiler: EmbLinkOs/EmbCC `a3c3ef4c81f9de977b8741ed16072cadca921788`. `manifest.json` records tool versions/hashes, commands/exit codes, build and runner identities, configuration and file hashes. No compiler source was changed. Read the scoped [integration report](../../../../../docs/engineering/embcc-native-linux.md).

Each preset directory contains actual Ninja commands (including the kernel launcher), configuration, generated storage, both compilers' layout inventories, clean build/test logs and unfiltered 40-schedule campaigns for seeds 3/5. All four builds passed twelve CTest targets each. Each of eight model campaigns passed 1,400 executions, skipped one unsupported scenario and had zero divergences. The launcher, storage and model/tooling checks passed 2/1/68 tests. Individual C cases and intentional fault expectations are in executable output, separate from CTest target counts.

`investigation/` preserves initial pre-integration inputs, controls and expected failures. These probes preceded the clean execution capture and do not inherit its source identity: initial POSIX probing used main `73cde48f837285bf5868be199efe527da871ec85`; compiler/layout rejection and GCC control used the implementation working tree subsequently committed as `df32ec2`. The rejection hook is an intentional test input, never a shipped build setting. The GCC control passed twelve CTest targets.

The small pthread control used the archived `thread.c` and these host-specific commands (replace paths for another checkout/tool installation):

```sh
/workspace/EmbCC/embcc --target=x86_64-linux-gnu -nostdinc \
  -isystem /usr/include/x86_64-linux-gnu -isystem /usr/include \
  -isystem /usr/lib/gcc/x86_64-linux-gnu/14/include \
  -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -fgnuc-version=13.0.0 \
  -O1 -c build/embcc-posix-probe/thread.c -o build/embcc-posix-probe/thread.o
gcc -pthread -no-pie build/embcc-posix-probe/thread.o -o build/embcc-posix-probe/thread
./build/embcc-posix-probe/thread
```

Compile/link/run passed; the output is in `pthread-run.log`. The archived `toolchain.cmake` then configured the complete native build with those system headers and host linker. That build **failed as expected** at `sys/types.h`'s unsupported `__mode__(__word__)` attribute. This is not a successful all-EmbCC program qualification.

The negative/control commands were:

```sh
# Expected configure failure: selected compiler identifies as GCC, not EmbCC.
cmake --preset native-embcc -B build/embcc-reject-compiler \
  -DEMB_KERNEL_COMPILER=/usr/bin/gcc

# Expected configure success, then layout-comparison build failure.
# Restore the archived reject-layout.cmake to this build-only path first.
cmake --preset native-embcc -B build/embcc-reject-layout \
  -DEMB_KERNEL_COMPILER=/workspace/EmbCC/embcc \
  -DCMAKE_PROJECT_INCLUDE=/workspace/EmbLinkRTOS/build/embcc-posix-probe/reject-layout.cmake
cmake --build build/embcc-reject-layout --target emb_kernel

cmake --preset native-gcc
cmake --build --preset native-gcc
ctest --preset native-gcc --no-tests=error
```

`raw-logs.json` keeps original text, including trailing whitespace removed from readable `.log` files. Current-source identities/configurations and output hashes support these bounded local claims; they do not prove general C ABI correctness, an all-EmbCC host build, embedded runtime behavior, physical timing, or the cause of the historical seed-5 finding. CI's current launcher contract uses a controlled executable and is separate from the actual local EmbCC compiler runs.
