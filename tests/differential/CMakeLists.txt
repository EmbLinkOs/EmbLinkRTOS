# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The differential runner (TEST-008): native only, needs the trace sink and every object.
if(CONFIG_EMB_ARCH_NATIVE AND CONFIG_EMB_TRACE AND CONFIG_EMB_SEM AND CONFIG_EMB_MUTEX AND CONFIG_EMB_NOTIFY)
  emb_add_executable(diff_runner diff_runner.c)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME differential_bridge
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/model/bridge.py"
                     --runner "$<TARGET_FILE:diff_runner>" --runs 6 --seed 1)
    set_tests_properties(differential_bridge PROPERTIES TIMEOUT 600)
  endif()
endif()
