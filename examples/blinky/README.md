<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Firat Hajiyev -->
# Blinky

The blinky example toggles the board LED every 500 ms without accumulating
timing drift. It also prints each transition, so boards with an emulator or a
serial console show the LED state.

The native port uses virtual time, so the example exits after 10 toggles.
Configure and build it for the native port:

```sh
cmake -S examples/blinky -B build/blinky-native -G Ninja -DEMB_BOARD=native
cmake --build build/blinky-native
./build/blinky-native/blinky
```

Build and run it on the MPS2 AN386 emulator:

```sh
cmake -S examples/blinky -B build/blinky-mps2 -G Ninja -DEMB_BOARD=mps2_an386
cmake --build build/blinky-mps2
cmake --build build/blinky-mps2 --target run_blinky
```

The same source builds for every board that implements the board LED API. It
has been run on the native port and on `mps2_an386` under QEMU; no physical
board is required.
