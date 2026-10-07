# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
if(CONFIG_EMB_ARCH STREQUAL "avr")
  set(_platform platform_avr.c)
else()
  set(_platform platform_native.c)
endif()
add_library(emb_test STATIC emb_test.c ${_platform})
target_include_directories(emb_test PUBLIC .)
target_link_libraries(emb_test PUBLIC emb_api emb_arch emb_board PRIVATE emb_flags)
