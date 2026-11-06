# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Header dependencies for EmbCC builds: EmbCC writes GCC-format dependency files (-MD -MT -MF),
# but CMake has no compiler module for it and asks for none. Without these lines Ninja never
# learns which headers an object includes, and a changed header, the generated <emb/config.h>
# above all, leaves stale objects behind: the ESP32 build kept a 40 MHz tick after the board
# moved to 160 MHz (2026-11-06). Included by every EmbCC toolchain file.
foreach(_emb_lang C ASM)
  set(CMAKE_DEPFILE_FLAGS_${_emb_lang} "-MD -MT <DEP_TARGET> -MF <DEP_FILE>")
  set(CMAKE_${_emb_lang}_DEPFILE_FORMAT gcc)
  set(CMAKE_${_emb_lang}_DEPENDS_USE_COMPILER TRUE)
endforeach()
