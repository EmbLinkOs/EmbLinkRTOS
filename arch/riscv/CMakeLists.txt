# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
enable_language(ASM)
# the SoC's tick and interrupt controller (rv_soc.h)
if(CONFIG_EMB_RV_ESP32C3)
  set(_emb_rv_soc rv_esp32c3.c)
else()
  set(_emb_rv_soc rv_clint.c)
endif()
add_library(emb_arch STATIC rv_port.c ${_emb_rv_soc} rv_switch.S)
target_include_directories(emb_arch PUBLIC include)
target_link_libraries(emb_arch PUBLIC emb_api emb_board PRIVATE emb_flags INTERFACE emb_kernel)
add_dependencies(emb_arch emb_storage_h)
