# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The public API: an interface target with the include paths, plus the generated
# <emb/storage.h> (SPEC-001 §6.2, SPEC-009): a probe object compiled against the real
# kernel layout is inspected with nm, so the storage sizes are measured, never guessed.

add_library(emb_api INTERFACE)
target_include_directories(emb_api INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${EMB_GENERATED_DIR}/include"
  "${CMAKE_SOURCE_DIR}/arch/${CONFIG_EMB_ARCH}/include")

# -- storage.h ---------------------------------------------------------------------
set(EMB_STORAGE_HEADER "${EMB_GENERATED_DIR}/include/emb/storage.h")
add_library(emb_storage_probe OBJECT "${CMAKE_SOURCE_DIR}/kernel/object/storage_probe.c")
target_include_directories(emb_storage_probe PRIVATE
  "${CMAKE_SOURCE_DIR}/kernel/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${EMB_GENERATED_DIR}/include"
  "${CMAKE_SOURCE_DIR}/arch/${CONFIG_EMB_ARCH}/include")
target_link_libraries(emb_storage_probe PRIVATE emb_flags)
if(NOT CMAKE_NM)
  set(CMAKE_NM nm)
endif()
add_custom_command(
  OUTPUT "${EMB_STORAGE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/storage/gen_storage.py"
          --nm "${CMAKE_NM}" --out "${EMB_STORAGE_HEADER}" $<TARGET_OBJECTS:emb_storage_probe>
  DEPENDS emb_storage_probe $<TARGET_OBJECTS:emb_storage_probe> "${CMAKE_SOURCE_DIR}/tools/storage/gen_storage.py"
  COMMENT "generating emb/storage.h from the layout probe"
  VERBATIM)
add_custom_target(emb_storage_h DEPENDS "${EMB_STORAGE_HEADER}")
add_dependencies(emb_api emb_storage_h)
