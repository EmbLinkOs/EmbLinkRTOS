# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Stress runs (SPEC-013 §5, A93): the native port's asynchronous interrupt mode. Not
# reproducible, so not differential evidence; the invariant checker is the oracle.
# Not under ThreadSanitizer: it reports any memory a signal handler and its own thread share as a
# race, and the handler here is an interrupt by design.
if(CONFIG_EMB_ARCH_NATIVE AND CONFIG_EMB_MSGQ AND CONFIG_EMB_MUTEX AND CONFIG_EMB_SEM AND CONFIG_EMB_TIMER
   AND NOT CONFIG_EMB_SCHED_TABLE AND NOT "${EMB_TOOLCHAIN_C_FLAGS}" MATCHES "sanitize=thread")
  emb_add_test(test_async_irq test_async_irq.c)
endif()
