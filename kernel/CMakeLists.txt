# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The kernel library. Feature files are excluded from the build, not #if'd away
# (CODING-STANDARD CS-10.6).

set(EMB_KERNEL_SOURCES
  exec/exec.c
  sched/sched.c
  wait/wait.c
  time/time.c
  thread/thread.c
  fault/fault.c
  trace/trace.c
  irq/irq.c)
if(CONFIG_EMB_NOTIFY)
  list(APPEND EMB_KERNEL_SOURCES notify/notify.c)
endif()
if(CONFIG_EMB_SEM)
  list(APPEND EMB_KERNEL_SOURCES sync/sem.c)
endif()
if(CONFIG_EMB_MUTEX)
  list(APPEND EMB_KERNEL_SOURCES sync/mutex.c)
endif()

add_library(emb_kernel STATIC ${EMB_KERNEL_SOURCES})
target_include_directories(emb_kernel PRIVATE include)
target_link_libraries(emb_kernel PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_arch)
add_dependencies(emb_kernel emb_storage_h)

# White-box tests and the native harness may include the private headers explicitly.
add_library(emb_kernel_private INTERFACE)
target_include_directories(emb_kernel_private INTERFACE include)
