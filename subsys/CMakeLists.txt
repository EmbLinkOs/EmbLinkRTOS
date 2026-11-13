# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# emb_subsys: the subsystems over the kernel (transports, and in M4 logging, trace and update),
# each behind its CONFIG_EMB_* symbol, compiled like the kernel and linked into every image,
# which keeps only what it calls. With none selected it is an empty interface library.
set(EMB_SUBSYS_SOURCES)
if(CONFIG_EMB_FRAME)
  list(APPEND EMB_SUBSYS_SOURCES transport/frame.c) # SPEC-015 §7
endif()

if(EMB_SUBSYS_SOURCES)
  add_library(emb_subsys STATIC ${EMB_SUBSYS_SOURCES})
  emb_use_kernel_compiler(emb_subsys)
  target_link_libraries(emb_subsys PUBLIC emb_api PRIVATE emb_flags)
else()
  add_library(emb_subsys INTERFACE)
endif()
