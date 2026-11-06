# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
enable_language(ASM)
add_library(emb_arch STATIC xt_port.c xt_vectors.S)
target_include_directories(emb_arch PUBLIC include)
target_link_libraries(emb_arch PUBLIC emb_api emb_board PRIVATE emb_flags INTERFACE emb_kernel)
add_dependencies(emb_arch emb_storage_h)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
  # newlib's reentrancy block outside ESP-IDF (xt_port.c), for Espressif's GCC and Clang alike;
  # EmbCC's C library keeps errno itself
  target_compile_definitions(emb_arch PRIVATE EMBN_XT_NEWLIB=1)
endif()
# The vectors include the generated <emb/config.h>. EmbCC writes no dependency file for assembly
# (cmake/toolchains/embcc-depfile.cmake), so the dependency is named here, for every compiler.
set_source_files_properties(xt_vectors.S PROPERTIES OBJECT_DEPENDS "${EMB_CONFIG_HEADER}")
