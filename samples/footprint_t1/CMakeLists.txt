# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
if(CONFIG_EMB_SEM AND CONFIG_EMB_NOTIFY)
  emb_add_executable(footprint_t1 main.c)
endif()
