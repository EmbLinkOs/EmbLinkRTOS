# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# ESP32-C3-DevKitM-1 (V1.0): the ESP32-C3-MINI-1, RV32IMC, booted by its ROM from the image
# at flash offset 0 (tools/esp/esp_image.py), everything in internal SRAM.
set(EMB_BOARD_ARCH riscv)
set(EMB_BOARD_VARIANT rv32imc)
set(EMB_BOARD_DEFAULT_PROFILE base)
set(EMB_BOARD_C_FLAGS -march=rv32imc_zicsr_zifencei -mabi=ilp32)
set(EMB_BOARD_FLASH_IMAGE esp32c3) # each image also as <name>.bin, what is flashed at 0x0
set(EMB_BOARD_EMBSIM_TARGET emblink/esp32c3_devkitm1) # EMB_TEST_EMULATOR=embsim: EmbSIM's DevKitM-1
