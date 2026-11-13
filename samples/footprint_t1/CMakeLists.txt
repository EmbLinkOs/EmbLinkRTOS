# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

if(CONFIG_EMB_SEM AND CONFIG_EMB_NOTIFY)
  emb_add_executable(footprint_t1 main.c)
endif()
