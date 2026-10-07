# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Locates the board, runs the configuration tool at configure time, and loads the
# resolved CONFIG_EMB_* values into CMake variables (05 §2.1).

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

# -- board ---------------------------------------------------------------------
file(GLOB _emb_board_cmake "${CMAKE_SOURCE_DIR}/boards/*/${EMB_BOARD}/board.cmake")
list(LENGTH _emb_board_cmake _n)
if(NOT _n EQUAL 1)
  file(GLOB _all "${CMAKE_SOURCE_DIR}/boards/*/*/board.cmake")
  set(_names "")
  foreach(_b ${_all})
    get_filename_component(_d "${_b}" DIRECTORY)
    get_filename_component(_nm "${_d}" NAME)
    string(APPEND _names " ${_nm}")
  endforeach()
  message(FATAL_ERROR "EMB_BOARD='${EMB_BOARD}' is not a board directory. Known boards:${_names}")
endif()
get_filename_component(EMB_BOARD_DIR "${_emb_board_cmake}" DIRECTORY)
include("${_emb_board_cmake}")   # EMB_BOARD_ARCH, EMB_BOARD_VARIANT, EMB_BOARD_DEFAULT_PROFILE, EMB_BOARD_MCU
if(NOT EMB_PROFILE)
  set(EMB_PROFILE "${EMB_BOARD_DEFAULT_PROFILE}")
endif()
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/configs/${EMB_PROFILE}.conf")
  message(FATAL_ERROR "EMB_PROFILE='${EMB_PROFILE}': no configs/${EMB_PROFILE}.conf")
endif()

# -- configuration -------------------------------------------------------------
set(EMB_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(EMB_CONFIG_HEADER "${EMB_GENERATED_DIR}/include/emb/config.h")
math(EXPR _host_word_bits "${CMAKE_SIZEOF_VOID_P} * 8")
set(_args
  "${CMAKE_SOURCE_DIR}/tools/kconfig/embconfig.py"
  --kconfig "${CMAKE_SOURCE_DIR}/Kconfig"
  --manifest "${CMAKE_SOURCE_DIR}/arch/${EMB_BOARD_ARCH}/arch.yaml"
  --variant "${EMB_BOARD_VARIANT}"
  --host-word-bits "${_host_word_bits}"
  --conf "${EMB_BOARD_DIR}/board.conf"
  --conf "${CMAKE_SOURCE_DIR}/configs/${EMB_PROFILE}.conf"
  --out-header "${EMB_CONFIG_HEADER}"
  --out-cmake "${EMB_GENERATED_DIR}/config.cmake"
  --out-json "${EMB_GENERATED_DIR}/config.json"
  --out-dotconfig "${CMAKE_BINARY_DIR}/.config")
set(_deps "${CMAKE_SOURCE_DIR}/Kconfig" "${EMB_BOARD_DIR}/board.conf"
          "${CMAKE_SOURCE_DIR}/configs/${EMB_PROFILE}.conf"
          "${CMAKE_SOURCE_DIR}/arch/${EMB_BOARD_ARCH}/arch.yaml"
          "${CMAKE_SOURCE_DIR}/tools/kconfig/embconfig.py")
foreach(_c ${EMB_CONF})
  if(NOT IS_ABSOLUTE "${_c}")
    set(_c "${CMAKE_SOURCE_DIR}/${_c}")
  endif()
  list(APPEND _args --conf "${_c}")
  list(APPEND _deps "${_c}")
endforeach()
foreach(_s ${EMB_SET})
  list(APPEND _args --set "${_s}")
endforeach()
file(GLOB_RECURSE _kconfigs "${CMAKE_SOURCE_DIR}/kernel/Kconfig" "${CMAKE_SOURCE_DIR}/arch/*/Kconfig" "${CMAKE_SOURCE_DIR}/arch/Kconfig")
list(APPEND _deps ${_kconfigs})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_deps})

execute_process(COMMAND "${Python3_EXECUTABLE}" -I ${_args}
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "configuration failed:\n${_out}${_err}")
endif()
include("${EMB_GENERATED_DIR}/config.cmake")

if(NOT CONFIG_EMB_ARCH STREQUAL EMB_BOARD_ARCH)
  message(FATAL_ERROR "manifest arch '${CONFIG_EMB_ARCH}' differs from the board's '${EMB_BOARD_ARCH}'")
endif()

function(emb_print_summary)
  message(STATUS "EmbLinkRTOS ${PROJECT_VERSION}: board ${EMB_BOARD} (${CONFIG_EMB_ARCH}/${CONFIG_EMB_ARCH_VARIANT}), "
                 "profile ${CONFIG_EMB_PROFILE}, checked=${CONFIG_EMB_CHECKED}, "
                 "config ${EMB_CONFIG_HASH}, compiler ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
endfunction()
