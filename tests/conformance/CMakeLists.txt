# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The conformance suite (05 §4.1): one executable per area, the same sources on every port.
emb_add_test(test_smoke test_smoke.c)
emb_add_test(test_sched test_sched.c)
emb_add_test(test_irq test_irq.c)
emb_add_test(test_thread test_thread.c)
emb_add_test(test_time test_time.c)
if(CONFIG_EMB_NOTIFY)
  emb_add_test(test_notify test_notify.c)
endif()
if(CONFIG_EMB_SEM)
  emb_add_test(test_sem test_sem.c)
endif()
if(CONFIG_EMB_MUTEX)
  emb_add_test(test_mutex test_mutex.c)
endif()
emb_add_test(test_first_execution test_first_execution.c)
