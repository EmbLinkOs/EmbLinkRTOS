# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# QEMU's RISC-V virt machine, one RV32IMAC hart in machine mode: the emulated RISC-V reference.
set(EMB_BOARD_ARCH riscv)
set(EMB_BOARD_VARIANT rv32imac)
set(EMB_BOARD_DEFAULT_PROFILE base)
set(EMB_BOARD_C_FLAGS -march=rv32imac_zicsr_zifencei -mabi=ilp32)
set(EMB_BOARD_QEMU_ARGS -M virt -icount shift=3,sleep=off -bios none -kernel) # deterministic time
