# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# ESP32 development board with the ESP-WROOM-32 module (DevKitC and its "ESP-32S" copies): the
# classic ESP32's PRO_CPU, booted by its ROM from the image at flash offset 0x1000
# (tools/esp/esp_image.py --chip esp32), everything in internal SRAM (ADR-054).
set(EMB_BOARD_ARCH xtensa)
set(EMB_BOARD_VARIANT lx6)
set(EMB_BOARD_DEFAULT_PROFILE base)
set(EMB_BOARD_C_FLAGS)
set(EMB_BOARD_FLASH_IMAGE esp32) # each image also as <name>.bin, what is flashed at 0x1000
set(EMB_BOARD_RESETS_IN hil) # RTC_CNTL_SW_SYS_RST keeps SRAM2, where the test record lives (A89)
