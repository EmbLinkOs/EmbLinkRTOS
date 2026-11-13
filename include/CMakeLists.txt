# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# The public API: an interface target with the include paths, plus the generated
# <emb/storage.h> (SPEC-001 §6.2, SPEC-009): a probe object compiled against the real
# kernel layout carries text markers with the sizes, so they are measured, never guessed.

add_library(emb_api INTERFACE)
target_include_directories(emb_api INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${EMB_GENERATED_DIR}/include"
  "${EMB_BASE}/arch/${CONFIG_EMB_ARCH}/include"
  ${EMB_BOARD_INCLUDE_DIRS})

# -- storage.h ---------------------------------------------------------------------
set(EMB_STORAGE_HEADER "${EMB_GENERATED_DIR}/include/emb/storage.h")
add_library(emb_storage_probe OBJECT "${EMB_BASE}/kernel/object/storage_probe.c")
target_include_directories(emb_storage_probe PRIVATE
  "${EMB_BASE}/kernel/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${EMB_GENERATED_DIR}/include"
  "${EMB_BASE}/arch/${CONFIG_EMB_ARCH}/include"
  ${EMB_BOARD_INCLUDE_DIRS})
target_link_libraries(emb_storage_probe PRIVATE emb_flags)
# its markers must be real object code, never LTO bitcode (ADR-056)
set_target_properties(emb_storage_probe PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
emb_use_kernel_compiler(emb_storage_probe)
add_custom_command(
  OUTPUT "${EMB_STORAGE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" -I "${EMB_BASE}/tools/storage/gen_storage.py"
          --out "${EMB_STORAGE_HEADER}" $<TARGET_OBJECTS:emb_storage_probe>
  DEPENDS emb_storage_probe $<TARGET_OBJECTS:emb_storage_probe> "${EMB_BASE}/tools/storage/gen_storage.py"
  COMMENT "generating emb/storage.h from the layout probe"
  VERBATIM)
add_custom_target(emb_storage_h DEPENDS "${EMB_STORAGE_HEADER}")
add_dependencies(emb_api emb_storage_h)
