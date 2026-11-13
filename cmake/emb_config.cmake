# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Locates the board, runs the configuration tool at configure time, and loads the
# resolved CONFIG_EMB_* values into CMake variables (05 §2.1).

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

# -- board ---------------------------------------------------------------------
# A hand-written board has boards/<vendor>/<board>/board.cmake; a board described in YAML has
# only board.yaml and the generator's generated/ directory, which then is EMB_BOARD_DIR (SPEC-014).
# Boards are looked up in the tree and in each EMB_BOARD_ROOT, a directory that holds the same
# boards/<vendor>/<board>/ layout: a project's own boards, kept outside the tree (A76).
set(EMB_BOARD_ROOT "" CACHE STRING "Extra board roots, semicolon separated: each holds boards/<vendor>/<board>/ (relative to the source tree)")
set(_emb_roots "${EMB_BASE}")
foreach(_r ${EMB_BOARD_ROOT})
  get_filename_component(_r "${_r}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}") # relative to the project: the RTOS, or an application
  if(NOT IS_DIRECTORY "${_r}/boards")
    message(FATAL_ERROR "EMB_BOARD_ROOT: ${_r} has no boards/ directory (a board root holds boards/<vendor>/<board>/)")
  endif()
  list(APPEND _emb_roots "${_r}")
endforeach()
# An application with a system description (SPEC-014 §5, A125): its board is one of the
# system's, generated with the system's settings into generated/<board>/ beside it; EMB_BOARD
# picks it, the system's first board by default.
set(EMB_SYSTEM "" CACHE FILEPATH "The application's system.yaml (SPEC-014 §5), relative to its source tree")
set(_emb_board_cmake "")
set(_all "")
if(EMB_SYSTEM)
  get_filename_component(EMB_SYSTEM "${EMB_SYSTEM}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  get_filename_component(EMB_SYSTEM_DIR "${EMB_SYSTEM}" DIRECTORY)
  file(GLOB _sys_json "${EMB_SYSTEM_DIR}/generated/*/docs/system.json")
  if(NOT _sys_json)
    message(FATAL_ERROR "EMB_SYSTEM: ${EMB_SYSTEM} has no generated/<board>/; "
                        "run python3 -I hw/gen/emb-hwgen.py generate --system ${EMB_SYSTEM}")
  endif()
  list(GET _sys_json 0 _one)
  file(READ "${_one}" _json)
  string(JSON _nboards LENGTH "${_json}" boards)
  set(_sys_boards "")
  math(EXPR _last "${_nboards} - 1")
  foreach(_i RANGE ${_last})
    string(JSON _b GET "${_json}" boards ${_i})
    list(APPEND _sys_boards "${_b}")
  endforeach()
  if(NOT EMB_BOARD)
    list(GET _sys_boards 0 EMB_BOARD)
  endif()
  if(NOT EMB_BOARD IN_LIST _sys_boards)
    string(REPLACE ";" " " _names "${_sys_boards}")
    message(FATAL_ERROR "EMB_BOARD='${EMB_BOARD}' is not one of the system's boards: ${_names}")
  endif()
  set(_emb_board_cmake "${EMB_SYSTEM_DIR}/generated/${EMB_BOARD}/build/board.cmake")
  if(NOT EXISTS "${_emb_board_cmake}")
    message(FATAL_ERROR "EMB_SYSTEM: no generated/${EMB_BOARD}/; "
                        "run python3 -I hw/gen/emb-hwgen.py generate --system ${EMB_SYSTEM}")
  endif()
  set(_emb_roots "")
endif()
foreach(_r ${_emb_roots})
  file(GLOB _found "${_r}/boards/*/${EMB_BOARD}/board.cmake" "${_r}/boards/*/${EMB_BOARD}/generated/build/board.cmake")
  list(APPEND _emb_board_cmake ${_found})
  file(GLOB _found "${_r}/boards/*/*/board.cmake" "${_r}/boards/*/*/generated/build/board.cmake")
  list(APPEND _all ${_found})
endforeach()
list(LENGTH _emb_board_cmake _n)
if(_n GREATER 1)
  string(REPLACE ";" "\n  " _where "${_emb_board_cmake}")
  message(FATAL_ERROR "EMB_BOARD='${EMB_BOARD}' names more than one board:\n  ${_where}\nrename one; board names are unique across the tree and EMB_BOARD_ROOT")
endif()
if(NOT _n EQUAL 1)
  set(_names "")
  foreach(_b ${_all})
    string(REGEX REPLACE "/generated/build/board.cmake$|/board.cmake$" "" _d "${_b}")
    get_filename_component(_nm "${_d}" NAME)
    string(APPEND _names " ${_nm}")
  endforeach()
  message(FATAL_ERROR "EMB_BOARD='${EMB_BOARD}' is not a board directory. Known boards:${_names}")
endif()
get_filename_component(EMB_BOARD_DIR "${_emb_board_cmake}" DIRECTORY)
set(EMB_BOARD_INCLUDE_DIRS "${EMB_BOARD_DIR}/include")
include("${_emb_board_cmake}")   # EMB_BOARD_ARCH, EMB_BOARD_VARIANT, EMB_BOARD_DEFAULT_PROFILE, EMB_BOARD_MCU
if(DEFINED EMB_BOARD_GENERATED_DIR)
  # a generated board (A126): its files are in config/, include/, src/, linker/, build/ and
  # docs/ under its generated directory, which is the board directory
  set(EMB_BOARD_DIR "${EMB_BOARD_GENERATED_DIR}")
endif()
if(NOT DEFINED EMB_BOARD_CONF)
  set(EMB_BOARD_CONF "${EMB_BOARD_DIR}/board.conf")  # a hand-written board's
endif()
if(NOT DEFINED EMB_BOARD_HW_JSON)
  set(EMB_BOARD_HW_JSON "${EMB_BOARD_DIR}/hw.json")
endif()
if(NOT DEFINED EMB_BOARD_BUILD_DIR)
  set(EMB_BOARD_BUILD_DIR "${EMB_BOARD_DIR}")
endif()
if(DEFINED EMB_BOARD_HW_INPUTS)
  # a generated board: refuse outputs older than their descriptions (HW-003), and reconfigure
  # when a description changes
  foreach(_in ${EMB_BOARD_HW_INPUTS})
    string(REGEX MATCH "^([^=]+)=([0-9a-f]+)$" _m "${_in}")
    set(_rel "${CMAKE_MATCH_1}")
    set(_want "${CMAKE_MATCH_2}")
    string(REGEX REPLACE "/generated$" "" _bd "${EMB_BOARD_DIR}")
    if(EMB_SYSTEM)
      set(_bd "${EMB_SYSTEM_DIR}")  # a system outside the tree records its files from its directory
    endif()
    if(_rel MATCHES "^@board/(.*)$")
      # a board outside the tree records its own files from its directory (emb-hwgen, A76)
      get_filename_component(_path "${_bd}/${CMAKE_MATCH_1}" ABSOLUTE)
    else()
      set(_path "${EMB_BASE}/${_rel}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_path}")
    if(NOT EXISTS "${_path}")
      message(FATAL_ERROR "${EMB_BOARD}: generated/ names ${_rel}, which does not exist")
    endif()
    file(SHA256 "${_path}" _have)
    if(NOT _have STREQUAL _want)
      file(RELATIVE_PATH _rbd "${EMB_BASE}" "${_bd}")
      if(_rbd MATCHES "^\\.\\./")
        set(_rbd "${_bd}")   # outside the tree: its absolute path
      endif()
      if(EMB_SYSTEM)
        message(FATAL_ERROR "${EMB_BOARD}: ${_rel} changed since generated/ was written; "
                            "run python3 -I hw/gen/emb-hwgen.py generate --system ${EMB_SYSTEM}")
      endif()
      message(FATAL_ERROR "${EMB_BOARD}: ${_rel} changed since generated/ was written; "
                          "run python3 -I hw/gen/emb-hwgen.py generate --board ${_rbd}")
    endif()
  endforeach()
endif()
if(NOT EMB_PROFILE)
  set(EMB_PROFILE "${EMB_BOARD_DEFAULT_PROFILE}")
endif()
if(NOT EXISTS "${EMB_BASE}/configs/${EMB_PROFILE}.conf")
  message(FATAL_ERROR "EMB_PROFILE='${EMB_PROFILE}': no configs/${EMB_PROFILE}.conf")
endif()

# -- configuration -------------------------------------------------------------
set(EMB_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(EMB_CONFIG_HEADER "${EMB_GENERATED_DIR}/include/emb/config.h")
math(EXPR _host_word_bits "${CMAKE_SIZEOF_VOID_P} * 8")
set(_args
  "${EMB_BASE}/tools/kconfig/embconfig.py"
  --kconfig "${EMB_BASE}/Kconfig"
  --manifest "${EMB_BASE}/arch/${EMB_BOARD_ARCH}/arch.yaml"
  --variant "${EMB_BOARD_VARIANT}"
  --host-word-bits "${_host_word_bits}"
  --conf "${EMB_BOARD_CONF}"
  --conf "${EMB_BASE}/configs/${EMB_PROFILE}.conf"
  --out-header "${EMB_CONFIG_HEADER}"
  --out-cmake "${EMB_GENERATED_DIR}/config.cmake"
  --out-json "${EMB_GENERATED_DIR}/config.json"
  --out-dotconfig "${CMAKE_BINARY_DIR}/.config")
set(_deps "${EMB_BASE}/Kconfig" "${EMB_BOARD_CONF}"
          "${EMB_BASE}/configs/${EMB_PROFILE}.conf"
          "${EMB_BASE}/arch/${EMB_BOARD_ARCH}/arch.yaml"
          "${EMB_BASE}/tools/kconfig/embconfig.py")
foreach(_c ${EMB_CONF})
  if(NOT IS_ABSOLUTE "${_c}")
    # the project's own fragment first (an application's), else the RTOS's configs/
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_c}")
      set(_c "${CMAKE_SOURCE_DIR}/${_c}")
    else()
      set(_c "${EMB_BASE}/${_c}")
    endif()
  endif()
  list(APPEND _args --conf "${_c}")
  list(APPEND _deps "${_c}")
endforeach()
foreach(_s ${EMB_SET})
  list(APPEND _args --set "${_s}")
endforeach()
file(GLOB_RECURSE _kconfigs "${EMB_BASE}/kernel/Kconfig" "${EMB_BASE}/arch/*/Kconfig" "${EMB_BASE}/arch/Kconfig")
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
  message(STATUS "EmbLinkRTOS ${EmbLinkRTOS_VERSION}: board ${EMB_BOARD} (${CONFIG_EMB_ARCH}/${CONFIG_EMB_ARCH_VARIANT}), "
                 "profile ${CONFIG_EMB_PROFILE}, checked=${CONFIG_EMB_CHECKED}, "
                 "config ${EMB_CONFIG_HASH}, compiler ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
endfunction()
