# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# emb_subsys: the subsystems over the kernel (transports, and in M4 logging, trace and update),
# each behind its CONFIG_EMB_* symbol, compiled like the kernel and linked into every image,
# which keeps only what it calls. The console is always there: emb_board_puts(), plain or
# framed (A127).
set(EMB_SUBSYS_SOURCES console/console.c)
if(CONFIG_EMB_FRAME)
  list(APPEND EMB_SUBSYS_SOURCES transport/frame.c) # SPEC-015 §7
endif()
if(CONFIG_EMB_LOG)
  list(APPEND EMB_SUBSYS_SOURCES logging/log.c) # SPEC-015 §2, A127
endif()

add_library(emb_subsys STATIC ${EMB_SUBSYS_SOURCES})
emb_use_kernel_compiler(emb_subsys)
# embs/: the subsystems' own headers, for their tests (not an application interface)
target_include_directories(emb_subsys PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(emb_subsys PUBLIC emb_api PRIVATE emb_flags)
