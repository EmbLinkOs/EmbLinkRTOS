# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Native test suites (05 §4.3). Each test executable runs the kernel in deterministic
# virtual-time mode and exits with 0 on success (SPEC-013 §11).
add_subdirectory(framework)
add_subdirectory(conformance)
add_subdirectory(benchmarks)
