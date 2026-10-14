/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Compile with both compilers; compare markers, never link or execute this object.
 * Offset + 1 is encoded as a size with alignment 1 so offset zero is representable
 * by the existing storage generator. Member widths are compared separately.
 * This is a maintained layout inventory, supplemented by executed ABI crossings.
 */
#include <embk/kernel.h>
#include <embk/sync.h>

#include <stdalign.h>
#include <stddef.h>

#define TYPE(name, type) EMB_LAYOUT_MARKER(name, sizeof(type), alignof(type))
#define FIELD(name, type, member)                                      \
    EMB_LAYOUT_MARKER(name##_offset, offsetof(type, member) + 1u, 1u); \
    EMB_LAYOUT_MARKER(name##_width, sizeof(((type *)0)->member), 1u)

void emb_native_abi_probe(void);

void emb_native_abi_probe(void)
{
    TYPE(thread, embk_thread_t);
    FIELD(thread_arch, embk_thread_t, arch);
    FIELD(thread_obj, embk_thread_t, obj);
    FIELD(thread_base_prio, embk_thread_t, base_prio);
    FIELD(thread_eff_prio, embk_thread_t, eff_prio);
    FIELD(thread_wait_state, embk_thread_t, wait_state);
    FIELD(thread_wait_reason, embk_thread_t, wait_reason);
    FIELD(thread_tflags, embk_thread_t, tflags);
    FIELD(thread_cancel_disable, embk_thread_t, cancel_disable);
    FIELD(thread_wait_gen, embk_thread_t, wait_gen);
    FIELD(thread_wake_data, embk_thread_t, wake_data);
    FIELD(thread_wait_queue, embk_thread_t, wait_queue);
    FIELD(thread_timeout, embk_thread_t, timeout);
    FIELD(thread_self_q, embk_thread_t, self_q);
    FIELD(thread_join_q, embk_thread_t, join_q);
    FIELD(thread_stack_base, embk_thread_t, stack_base);
    FIELD(thread_stack_size, embk_thread_t, stack_size);
#if CONFIG_EMB_CHECKED
    TYPE(thread_checked, embk_thread_t);
    FIELD(thread_checked_lock_depth, embk_thread_t, lock_depth);
#endif
#if !CONFIG_EMB_SCHED_TABLE
    TYPE(thread_list_sched, embk_thread_t);
    FIELD(thread_list_sched_wait_node, embk_thread_t, wait_node);
    FIELD(thread_list_sched_ready_node, embk_thread_t, ready_node);
    FIELD(thread_list_sched_index, embk_thread_t, index);
#endif
#if CONFIG_EMB_NOTIFY
    TYPE(thread_notify, embk_thread_t);
    FIELD(thread_notify_notify_bits, embk_thread_t, notify_bits);
    FIELD(thread_notify_notify_mask, embk_thread_t, notify_mask);
#endif
#if CONFIG_EMB_MUTEX
    TYPE(thread_mutex, embk_thread_t);
    FIELD(thread_mutex_owned, embk_thread_t, owned);
#endif
#if CONFIG_EMB_THREAD_LIST
    TYPE(thread_list, embk_thread_t);
    FIELD(thread_list_all_node, embk_thread_t, all_node);
#endif
#if CONFIG_EMB_TLS_SLOTS > 0
    TYPE(thread_tls, embk_thread_t);
    FIELD(thread_tls_tls, embk_thread_t, tls);
#endif
    TYPE(arch, emb_arch_tcb_t);
    FIELD(arch_sp, emb_arch_tcb_t, sp);
    FIELD(arch_ext, emb_arch_tcb_t, ext);
    TYPE(cpu, embk_cpu_t);
    FIELD(cpu_current, embk_cpu_t, current);
    FIELD(cpu_irq_nesting_depth, embk_cpu_t, irq_nesting_depth);
    FIELD(cpu_sched_lock_depth, embk_cpu_t, sched_lock_depth);
    FIELD(cpu_reschedule_pending, embk_cpu_t, reschedule_pending);
    FIELD(cpu_kernel_state, embk_cpu_t, kernel_state);
#if CONFIG_EMB_CHECKED
    TYPE(cpu_checked, embk_cpu_t);
    FIELD(cpu_checked_irq_lock_depth, embk_cpu_t, irq_lock_depth);
#endif
    TYPE(obj, embk_obj_t);
    FIELD(obj_type, embk_obj_t, type);
    FIELD(obj_state, embk_obj_t, state);
    FIELD(obj_flags, embk_obj_t, flags);
#if CONFIG_EMB_OBJ_NAMES
    TYPE(obj_names, embk_obj_t);
    FIELD(obj_names_name, embk_obj_t, name);
#endif
    TYPE(queue, embk_wait_queue_t);
    FIELD(queue_flags, embk_wait_queue_t, flags);
#if CONFIG_EMB_SCHED_TABLE
    TYPE(queue_table, embk_wait_queue_t);
    FIELD(queue_table_set, embk_wait_queue_t, set);
#endif
#if !CONFIG_EMB_SCHED_TABLE
    TYPE(queue_list, embk_wait_queue_t);
    FIELD(queue_list_waiters, embk_wait_queue_t, waiters);
#endif
    TYPE(timeout, embk_timeout_t);
    FIELD(timeout_deadline, embk_timeout_t, deadline);
#if CONFIG_EMB_SCHED_TABLE
    TYPE(timeout_table, embk_timeout_t);
    FIELD(timeout_table_next, embk_timeout_t, next);
#endif
#if !CONFIG_EMB_SCHED_TABLE
    TYPE(timeout_list, embk_timeout_t);
    FIELD(timeout_list_node, embk_timeout_t, node);
#endif
    TYPE(list, embk_list_node_t);
    FIELD(list_next, embk_list_node_t, next);
    FIELD(list_prev, embk_list_node_t, prev);
#if CONFIG_EMB_SEM
    TYPE(sem, embk_sem_t);
    FIELD(sem_obj, embk_sem_t, obj);
    FIELD(sem_waiters, embk_sem_t, waiters);
    FIELD(sem_count, embk_sem_t, count);
    FIELD(sem_max, embk_sem_t, max);
#endif
#if CONFIG_EMB_SEM && CONFIG_EMB_NOTIFY
    TYPE(sem_notify, embk_sem_t);
    FIELD(sem_notify_bind_thread, embk_sem_t, bind_thread);
    FIELD(sem_notify_bind_bit, embk_sem_t, bind_bit);
#endif
#if CONFIG_EMB_MUTEX
    TYPE(mutex, embk_mutex_t);
    FIELD(mutex_obj, embk_mutex_t, obj);
    FIELD(mutex_waiters, embk_mutex_t, waiters);
    FIELD(mutex_owner, embk_mutex_t, owner);
    FIELD(mutex_owned_next, embk_mutex_t, owned_next);
    FIELD(mutex_protocol, embk_mutex_t, protocol);
    FIELD(mutex_ceiling, embk_mutex_t, ceiling);
    FIELD(mutex_count, embk_mutex_t, count);
    FIELD(mutex_reserved_, embk_mutex_t, reserved_);
#endif
    TYPE(thread_attr, emb_thread_attr_t);
    FIELD(thread_attr_name, emb_thread_attr_t, name);
    FIELD(thread_attr_stack, emb_thread_attr_t, stack);
    FIELD(thread_attr_stack_size, emb_thread_attr_t, stack_size);
    FIELD(thread_attr_priority, emb_thread_attr_t, priority);
    FIELD(thread_attr_flags, emb_thread_attr_t, flags);
    FIELD(thread_attr_cpu_affinity, emb_thread_attr_t, cpu_affinity);
    FIELD(thread_attr_reserved_, emb_thread_attr_t, reserved_);
    TYPE(sem_attr, emb_sem_attr_t);
    FIELD(sem_attr_name, emb_sem_attr_t, name);
    FIELD(sem_attr_initial, emb_sem_attr_t, initial);
    FIELD(sem_attr_max, emb_sem_attr_t, max);
    FIELD(sem_attr_flags, emb_sem_attr_t, flags);
    FIELD(sem_attr_reserved_, emb_sem_attr_t, reserved_);
    TYPE(mutex_attr, emb_mutex_attr_t);
    FIELD(mutex_attr_name, emb_mutex_attr_t, name);
    FIELD(mutex_attr_protocol, emb_mutex_attr_t, protocol);
    FIELD(mutex_attr_ceiling, emb_mutex_attr_t, ceiling);
    FIELD(mutex_attr_flags, emb_mutex_attr_t, flags);
    FIELD(mutex_attr_reserved_, emb_mutex_attr_t, reserved_);
    TYPE(fault, emb_fault_info_t);
    FIELD(fault_fault_class, emb_fault_info_t, fault_class);
    FIELD(fault_argument, emb_fault_info_t, argument);
    FIELD(fault_code, emb_fault_info_t, code);
    FIELD(fault_api_id, emb_fault_info_t, api_id);
    FIELD(fault_thread_index, emb_fault_info_t, thread_index);
    FIELD(fault_context, emb_fault_info_t, context);
    FIELD(fault_address, emb_fault_info_t, address);
    FIELD(fault_pc, emb_fault_info_t, pc);
    FIELD(fault_sp, emb_fault_info_t, sp);
    FIELD(fault_where, emb_fault_info_t, where);
    TYPE(trace, emb_trace_event_t);
    FIELD(trace_id, emb_trace_event_t, id);
    FIELD(trace_cpu, emb_trace_event_t, cpu);
    FIELD(trace_context, emb_trace_event_t, context);
    FIELD(trace_ticks, emb_trace_event_t, ticks);
    FIELD(trace_a0, emb_trace_event_t, a0);
    FIELD(trace_a1, emb_trace_event_t, a1);
    FIELD(trace_a2, emb_trace_event_t, a2);
    TYPE(thread_handle, emb_thread_t);
    FIELD(thread_handle_raw, emb_thread_t, raw);
    TYPE(sem_handle, emb_sem_t);
    FIELD(sem_handle_raw, emb_sem_t, raw);
    TYPE(mutex_handle, emb_mutex_t);
    FIELD(mutex_handle_raw, emb_mutex_t, raw);
    TYPE(instant, emb_instant_t);
    FIELD(instant_ticks, emb_instant_t, ticks);
    TYPE(duration, emb_duration_t);
    FIELD(duration_ticks, emb_duration_t, ticks);
    TYPE(timeout_arg, emb_timeout_t);
    FIELD(timeout_arg_ticks, emb_timeout_t, ticks);
}
