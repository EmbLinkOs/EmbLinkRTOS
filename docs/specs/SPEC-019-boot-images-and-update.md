# SPEC-019 - Boot, Images and Update: MCUboot, Slots, Confirm or Revert, Power-Loss Evidence

**Implementation and release scope:** [Project status](../STATUS.md) records the implemented subset; [roadmap](../architecture/07-roadmap.md) defines release gates. Nothing in this specification is implemented yet; M4.5 implements it.

**Status:** Proposed 2026-11-09 for the project owner's review (M4.0).
**Requirements:** `docs/requirements/BOOT.md` (BOOT-001, 002, 005 to 008; 003 and 004 are not in M4's scope), SEC-013, SEC-014, DRV-007.
**Builds on:** 04 §5; ADR-008 (MCUboot's image format), ADR-058 (upstream MCUboot, proposed); SPEC-014 §4 (`flash_partitions`), SPEC-016 §7 (polled mode), SPEC-017 §9 (the flash class), SPEC-015 §7 (framing, for the update protocol).

---

## 1. Scope

How an image is built, signed, stored, updated and booted, and how its safety against power loss is shown. In M4: one image per board, on the NUCLEO-F446RE and the Pico 2 W. Out of scope: multi-image boot (BOOT-004), measured boot for attestation (BOOT-003: no reference board provisions a root of trust), encrypted images, and the RP2350 boot ROM's own A/B scheme.

## 2. The chain

```
reset -> MCUboot (its own image, at the boot base)
           |-- reads both slots' headers and trailers
           |-- finishes or reverts an interrupted swap, then performs a pending one
           |-- checks the primary slot's hash and signature
           '-- jumps: VTOR, MSP from the image's vector table, pending interrupts cleared
        -> the application, in slot 0, after its 0x200-byte header
```

MCUboot is upstream, pinned, its needed parts vendored in `third_party/mcuboot/` (ADR-058). `boot/mcuboot/` is the port: its configuration, a flash map over the flash class in polled mode (SPEC-016 §7, DRV-007), the console for its messages, and the jump. It builds as a separate image per board, with GCC and Clang.

## 3. Layout (BOOT-008)

A board that declares a bootloader names its partitions and the boot settings:

```yaml
flash_partitions:
  - { name: bootloader, base: 0x08000000, size: 48K }    # sectors 0-2
  - { name: config,     base: 0x0800C000, size: 16K }    # sector 3: anti-rollback counter, settings
  - { name: log,        base: 0x08010000, size: 64K }    # sector 4: the field node's log
  - { name: slot0,      base: 0x08020000, size: 128K }   # sector 5: the running image
  - { name: slot1,      base: 0x08040000, size: 128K }   # sector 6: the update
  - { name: scratch,    base: 0x08060000, size: 128K }   # sector 7: swap scratch
boot: { loader: mcuboot, header_size: 0x200, mode: swap_scratch }
```

| | NUCLEO-F446RE (512 KiB) | Pico 2 W (4 MiB, 4 KiB sectors) |
|---|---|---|
| Mode | swap with scratch: its 128 KiB sectors make swap-move cost a whole extra sector per slot | swap-move: one extra 4 KiB sector in slot 0 |
| Bootloader | sectors 0 to 2, 48 KiB; if MCUboot does not fit, sectors 0 to 3 and the log shrinks | the first 64 KiB, with the boot ROM's IMAGE_DEF block, which the ROM requires to boot it |
| Slots | 128 KiB each | 1 MiB each, plus one sector in slot 0 |

- **With `boot:`**, the generator links the application at slot 0 plus `header_size` and lifts today's refusal of an application partition that does not start at the boot base (`hw/gen/emb_hwgen/model.py`).
- **Without `boot:`**, the layout and the refusal are unchanged.
- **The reset path writes VTOR** from a linker symbol, the image's own vector table. SPEC-011 §4 already gives `emb_arch_early_init()` the vector table base; the Cortex-M port, which has always booted from the boot base, implements it with M4.5. On the F446 the vector table needs 512-byte alignment: slot 0 plus 0x200 gives it.
- **The generator checks** that every partition is whole sectors of the flash's geometry, that the slots are equal in size, and that the scratch is at least as large as the largest sector a slot uses.

## 4. Images (BOOT-001)

- **Format:** MCUboot's header (magic, load address, header size, image size, version, flags), the image, then the TLV area: SHA-256, the signature (ECDSA P-256) and the security counter.
- **Signing:** `imgtool sign`, pinned in `tools/requirements-image.txt`, wrapped as `emb image sign`. `emb image verify` checks with `imgtool` and with `tools/image/mcuimg.py`, an independent reader, so the format is checked by two implementations.
- **Keys (SEC-013):** no private key in the tree but one, the test key, in `boot/mcuboot/keys/test-ecdsa-p256.pem`, named as such. A build that embeds the test key's public half is a test build: its bootloader prints so at every boot, and a production configuration refuses to build with it.
- **Version:** the application's semantic version, written into the header by `emb image sign` from the build.

## 5. Update (BOOT-002, BOOT-006)

### 5.1 The application's API

```c
emb_status_t emb_boot_confirm(void);                        /* this image works: keep it */
emb_status_t emb_boot_request_upgrade(emb_boot_mode_t mode); /* EMB_BOOT_TEST or EMB_BOOT_PERMANENT */
emb_status_t emb_boot_slot_info(unsigned slot, emb_boot_slot_t *out);  /* version, state, hash */
```

Over MCUboot's `bootutil_public` (set-pending, set-confirmed), reading and writing the slots' trailers through the flash class.

### 5.2 Confirm or revert

1. The new image is written into slot 1 and checked (hash, signature) before anything else.
2. `emb_boot_request_upgrade(EMB_BOOT_TEST)` marks it pending; the next reset swaps the slots.
3. The new image runs. After its own self-test it calls `emb_boot_confirm()`.
4. **Not confirmed** (it crashed, hung until the watchdog fired, or decided it failed): the next reset swaps back. The old image runs again, confirmed as it was.

`EMB_BOOT_PERMANENT` skips the test: the swap is final. It is for images already proven elsewhere.

### 5.3 Anti-rollback

The image's TLV carries a security counter. MCUboot refuses an image whose counter is below the one stored in the `config` partition, and raises the stored one when an image is confirmed. **OTP is not used:** the counter in flash can be reset by someone who reflashes the bootloader, which BOOT-002 records as partly met; anti-rollback in OTP is documented for a product, never burned by CI or the bench (SEC-014).

### 5.4 The update protocol

On the framed transport's channel 4 (SPEC-015 §7, M4.3): `begin` (size, version), `write` (offset, data, each frame CRC-checked), `finish` (the image's hash), `status`, `test`, `reset`. The device marks the image pending only after its TLV hash and signature check out. `emb update <port> <image>` is the host side. A transfer interrupted at any point leaves slot 0 untouched and slot 1 not pending.

## 6. Power-loss evidence (BOOT-007)

At three levels, each archived:

| Level | Where | How | Volume |
|---|---|---|---|
| **1. Exhaustive** | CI, native | bootutil, the flash map and a flash simulator that loses power at operation N, with torn writes (a page partly programmed, a sector partly erased); N sweeps every operation of an upgrade and of a revert | every N |
| **2. Deterministic** | both boards | `CONFIG_EMB_BOOT_FAULT_INJECT`: the flash map resets the part after operation N. N survives the reset in the F446's backup SRAM and in the RP2350's watchdog scratch registers. `tools/hil/update_powerloss.py` sweeps N | every N of one upgrade and one revert |
| **3. External, random** | both boards | the Pico 2 W's 3V3_EN pin pulled low by the bench's NUCLEO, a real loss of the RP2350's and its flash's power. The NUCLEO under test: NRST pulsed, at random delays. The analyzer records the cut, a flash-busy marker and the application's confirmed marker | at least 500 per board |

**After every interruption,** the next boot must run the old image or the new one, valid, and never neither. The record keeps which, and the recovery time from power to the confirmed marker.

**Stated plainly:** a reset by NRST is not a loss of power. Whether it aborts an STM32 flash operation in progress is part of what level 3 records. A real power cut on the NUCLEO needs a load switch on its supply (the optional part in [HIL](../engineering/hil.md)). On the Pico, the relay's TX is tri-stated during a cut, so the dead board is not powered through its UART pin.

## 7. Boot time (BOOT-005)

From reset release to the application's first marker, measured externally on both boards, with signature checking and with hash-only checking, and with and without a pending swap. Archived with the update runs.

## 8. Verification

| Requirement | How |
|---|---|
| BOOT-001 | `imgtool verify` and `mcuimg.py` accept every signed reference image; the bootloader refuses a modified one |
| BOOT-002 | §6's three levels; the anti-rollback refusal of a lower counter |
| BOOT-005 | §7's measurement, archived |
| BOOT-006 | confirm and revert, on native and both boards |
| BOOT-007 | §6's records |
| BOOT-008 | the generator's layouts with and without `boot:`; the application booting through MCUboot on both boards |
| SEC-013 | the test key's marking; a production configuration refusing it |
| SEC-014 | no flashing command or test writing an OTP or irreversible option-byte address |

## 9. Defaults to confirm at review

- Swap with scratch on the F446, swap-move on the RP2350.
- The F446's layout of §3: MCUboot in 48 KiB, falling back to 64 KiB.
- ECDSA P-256 signatures and SHA-256.
- The anti-rollback counter in flash, OTP documented only.
- One image per board in M4.
