# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The host remains a normal GCC project; only named freestanding targets use EmbCC.
if(EMB_NATIVE_MIXED_EMBCC)
  if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR
     NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR
     NOT CONFIG_EMB_ARCH STREQUAL "native" OR
     NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The mixed EmbCC build requires native Linux x86-64 with host GCC")
  endif()
  find_program(EMB_KERNEL_COMPILER NAMES embcc REQUIRED
               DOC "EmbCC executable for the native kernel and layout probes")
  execute_process(COMMAND "${EMB_KERNEL_COMPILER}" --version
                  RESULT_VARIABLE _embcc_result OUTPUT_VARIABLE _embcc_version)
  if(NOT _embcc_result EQUAL 0 OR NOT _embcc_version MATCHES "^EmbCC ")
    message(FATAL_ERROR "EMB_KERNEL_COMPILER must name a working EmbCC executable")
  endif()
  file(SHA256 "${EMB_KERNEL_COMPILER}" _embcc_sha256)
  string(REGEX MATCH "^[^\n]+" _embcc_version "${_embcc_version}")
  set(EMB_KERNEL_COMPILER_VERSION "${_embcc_version}" CACHE INTERNAL "Kernel compiler version" FORCE)
  set(EMB_KERNEL_COMPILER_SHA256 "${_embcc_sha256}" CACHE INTERNAL "Kernel compiler digest" FORCE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${EMB_KERNEL_COMPILER}")
  message(STATUS "Mixed native build: kernel=${EMB_KERNEL_COMPILER} (${_embcc_version}); host=${CMAKE_C_COMPILER}")
endif()

function(emb_use_kernel_compiler target)
  if(EMB_NATIVE_MIXED_EMBCC)
    set(_launcher "${CMAKE_SOURCE_DIR}/tools/toolchain/kernel_compile.py")
    set_property(TARGET ${target} PROPERTY C_COMPILER_LAUNCHER
      "${Python3_EXECUTABLE};${_launcher};${EMB_KERNEL_COMPILER}")
    get_target_property(_sources ${target} SOURCES)
    set_property(SOURCE ${_sources} APPEND PROPERTY OBJECT_DEPENDS
      "${_launcher}" "${EMB_KERNEL_COMPILER}")
  endif()
endfunction()

function(emb_check_native_abi)
  if(NOT EMB_NATIVE_MIXED_EMBCC)
    return()
  endif()
  foreach(_compiler host kernel)
    add_library(emb_${_compiler}_abi OBJECT "${CMAKE_SOURCE_DIR}/tools/toolchain/native_abi.c")
    target_include_directories(emb_${_compiler}_abi PRIVATE "${CMAKE_SOURCE_DIR}/kernel/include")
    target_link_libraries(emb_${_compiler}_abi PRIVATE emb_api emb_flags)
    if(_compiler STREQUAL "kernel")
      emb_use_kernel_compiler(emb_kernel_abi)
    endif()
    set(_header "${EMB_GENERATED_DIR}/abi/${_compiler}.h")
    add_custom_command(OUTPUT "${_header}"
      COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/storage/gen_storage.py"
              --out "${_header}" $<TARGET_OBJECTS:emb_${_compiler}_abi>
      DEPENDS emb_${_compiler}_abi $<TARGET_OBJECTS:emb_${_compiler}_abi>
              "${CMAKE_SOURCE_DIR}/tools/storage/gen_storage.py"
      VERBATIM)
  endforeach()
  add_custom_command(OUTPUT "${EMB_GENERATED_DIR}/abi/verified"
    COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${EMB_GENERATED_DIR}/abi/host.h" "${EMB_GENERATED_DIR}/abi/kernel.h"
    COMMAND "${CMAKE_COMMAND}" -E touch "${EMB_GENERATED_DIR}/abi/verified"
    DEPENDS "${EMB_GENERATED_DIR}/abi/host.h" "${EMB_GENERATED_DIR}/abi/kernel.h"
    COMMENT "checking GCC/EmbCC native sizes, alignments and member offsets"
    VERBATIM)
  add_custom_target(emb_native_abi DEPENDS "${EMB_GENERATED_DIR}/abi/verified")
  add_dependencies(emb_kernel emb_native_abi)
endfunction()
