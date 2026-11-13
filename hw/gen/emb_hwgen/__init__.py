# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""emb-hwgen: the hardware description generator (SPEC-014, HW-001 to HW-015)."""

VERSION = "0.1.0"
SCHEMA_VERSION = 0


class HwError(Exception):
    """One or more description errors, each 'file:line: node.path: message' (HW-002)."""

    def __init__(self, errors):
        self.errors = sorted(set(errors))
        super().__init__("\n".join(self.errors))
