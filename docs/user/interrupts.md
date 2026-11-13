# Interrupts

A handler that calls the kernel is **kernel-aware**: the kernel's critical sections mask it, and at its end the kernel may switch to a thread it woke. Every handler bound through the API is kernel-aware. A Cortex-M interrupt above `CONFIG_EMB_CM_KERNEL_BASEPRI` is never masked but must not call the kernel ([SPEC-002 §3](../specs/SPEC-002-interrupts-and-critical-sections.md)). The API does not bind such interrupts yet.

## Writing a handler

On Cortex-M and RISC-V, bind the handler at run time (`CONFIG_EMB_IRQ_DYNAMIC`, on by default there):

```c
#include <emb/emb.h>

static emb_sem_t rx_ready;

EMB_ISR(uart_rx_isr)                /* static void uart_rx_isr(void *emb_isr_arg_) */
{
    (void)emb_isr_arg_;
    /* read the byte and clear the device's flag first, then wake the thread */
    (void)emb_sem_give(rx_ready);   /* ISR-safe: never blocks */
}

void uart_start(emb_irq_t line)
{
    EMB_CHECK(emb_irq_connect(line, uart_rx_isr, NULL));
    EMB_CHECK(emb_irq_set_level(line, 1u)); /* 0 .. EMB_IRQ_LEVEL_KERNEL_MAX; larger is more urgent */
    EMB_CHECK(emb_irq_enable(line));
}
```

On AVR, handlers are the vectors themselves: `EMB_ISR(USART_RX) { ... }` defines the kernel-aware handler of the vector the board's `hw_config.h` names `EMB_VECTOR_USART_RX`, with no `emb_irq_connect()` (`CONFIG_EMB_IRQ_DYNAMIC` is off). `EMB_ISR_RAW(name)` defines a vector that does not call the kernel and costs no kernel entry.

## What a handler may call

Only the functions whose contract lists `isr` in `@ctx`; the table below is generated from them. A function marked as blocking takes a timeout: pass `EMB_NO_WAIT` from a handler. There are no separate `FromISR` variants. A thread a handler wakes runs at the outermost interrupt exit, when it is the most urgent ([KRN-SCH-021](../requirements/KRN-SCH.md)).

Calling anything else from a handler is misuse: in a checked build it faults with the context class (`EMB_FAULT_API_CONTEXT`), and in a release build it returns `EMB_EPERM`. The usual pattern is a handler that clears the device, gives a semaphore, sets a notification bit, sends to a message queue with `EMB_NO_WAIT`, or submits a work item, and a thread that does the rest.

## Nesting and stacks

- **Cortex-M** nests kernel-aware interrupts by level when `CONFIG_EMB_IRQ_NESTING` is on (the default), up to `CONFIG_EMB_IRQ_MAX_NESTING`. Handlers run on the interrupt stack, `CONFIG_EMB_ISR_STACK_SIZE` bytes (2048 by default). Size it from `emb_isr_stack_info()`, which reports the deepest use since start: every test run prints it as `ISR_STACK size=... used=...`. Checked builds check its lowest bytes at every outermost exit, and Armv8-M Mainline stops an overflow with MSPLIM ([A90](../specs/AMENDMENTS-M3.md)).
- **RISC-V** does not nest. Handlers run on a dedicated stack of `CONFIG_EMB_ISR_STACK_SIZE` bytes.
- **AVR** does not nest. A handler runs on the interrupted thread's stack, so every thread's stack holds `CONFIG_EMB_AVR_ISR_STACK_RESERVE` bytes for it: `EMB_THREAD_STACK_MIN` includes the reserve.
- **Every port:** checked builds also check the interrupted thread's stack guard at the outermost exit.

## The interrupt-safe functions

<!-- isr-safe:begin (tools/docs/isr_table.py) -->

155 functions, from the headers' `@ctx` tags.

| Function | Header | Blocks |
|---|---|---|
| `emb_check_status()` | `<emb/kernel.h>` | no |
| `emb_cond_attr_default()` | `<emb/cond.h>` | no |
| `emb_console_frame()` | `<emb/console.h>` | yes, in a thread outside a critical section: for the console, held by another thread's frame |
| `emb_context()` | `<emb/context.h>` | no |
| `emb_device_caps()` | `<emb/device.h>` | no |
| `emb_device_ready()` | `<emb/device.h>` | no |
| `emb_device_state()` | `<emb/device.h>` | no |
| `emb_device_status()` | `<emb/device.h>` | no |
| `emb_drv_cancel()` | `<emb/device.h>` | no |
| `emb_drv_complete()` | `<emb/device.h>` | no |
| `emb_drv_counter_alarm_cancel()` | `<emb/drv_counter.h>` | no |
| `emb_drv_counter_alarm_set()` | `<emb/drv_counter.h>` | no |
| `emb_drv_counter_channels()` | `<emb/drv_counter.h>` | no |
| `emb_drv_counter_fired()` | `<emb/drv_counter.h>` | no |
| `emb_drv_counter_now()` | `<emb/drv_counter.h>` | no |
| `emb_drv_counter_periodic()` | `<emb/drv_counter.h>` | no |
| `emb_drv_counter_periodic_stop()` | `<emb/drv_counter.h>` | no |
| `emb_drv_counter_rate()` | `<emb/drv_counter.h>` | no |
| `emb_drv_dma_start()` | `<emb/drv_dma.h>` | no |
| `emb_drv_dma_stop()` | `<emb/drv_dma.h>` | no |
| `emb_drv_entropy_board()` | `<emb/drv_entropy.h>` | no |
| `emb_drv_flash_geometry_get()` | `<emb/drv_flash.h>` | no |
| `emb_drv_flash_sector()` | `<emb/drv_flash.h>` | no |
| `emb_drv_gpio_assert()` | `<emb/drv_gpio.h>` | no |
| `emb_drv_gpio_configure()` | `<emb/drv_gpio.h>` | no |
| `emb_drv_gpio_read()` | `<emb/drv_gpio.h>` | no |
| `emb_drv_gpio_write()` | `<emb/drv_gpio.h>` | no |
| `emb_drv_i2c_req_listen()` | `<emb/drv_i2c.h>` | no |
| `emb_drv_i2c_req_transfer()` | `<emb/drv_i2c.h>` | no |
| `emb_drv_i2c_submit()` | `<emb/drv_i2c.h>` | no |
| `emb_drv_on_callback()` | `<emb/device.h>` | no |
| `emb_drv_on_notify()` | `<emb/device.h>` | no |
| `emb_drv_on_sem()` | `<emb/device.h>` | no |
| `emb_drv_on_work()` | `<emb/device.h>` | no |
| `emb_drv_request_init()` | `<emb/device.h>` | no |
| `emb_drv_rtc_alarm_cancel()` | `<emb/drv_rtc.h>` | no |
| `emb_drv_rtc_alarm_fired()` | `<emb/drv_rtc.h>` | no |
| `emb_drv_rtc_alarm_set()` | `<emb/drv_rtc.h>` | no |
| `emb_drv_rtc_backup_read()` | `<emb/drv_rtc.h>` | no |
| `emb_drv_rtc_backup_write()` | `<emb/drv_rtc.h>` | no |
| `emb_drv_rtc_get()` | `<emb/drv_rtc.h>` | no |
| `emb_drv_spi_begin()` | `<emb/drv_spi.h>` | no |
| `emb_drv_spi_end()` | `<emb/drv_spi.h>` | no |
| `emb_drv_spi_req_transfer()` | `<emb/drv_spi.h>` | no |
| `emb_drv_spi_submit()` | `<emb/drv_spi.h>` | no |
| `emb_drv_submit()` | `<emb/device.h>` | no |
| `emb_drv_uart_poll_read()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_poll_write()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_req_break()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_req_read()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_req_write()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_rx_byte()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_rx_error()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_rx_idle()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_rx_start()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_rx_stop()` | `<emb/drv_uart.h>` | no |
| `emb_drv_uart_submit()` | `<emb/drv_uart.h>` | no |
| `emb_drv_wdt_caused_reset()` | `<emb/drv_wdt.h>` | no |
| `emb_drv_wdt_feed()` | `<emb/drv_wdt.h>` | no |
| `emb_duration_is_saturated()` | `<emb/time.h>` | no |
| `emb_duration_to_ns()` | `<emb/time.h>` | no |
| `emb_dwork_cancel()` | `<emb/work.h>` | no |
| `emb_dwork_init()` | `<emb/work.h>` | no |
| `emb_dwork_reschedule()` | `<emb/work.h>` | no |
| `emb_dwork_schedule()` | `<emb/work.h>` | no |
| `emb_dwork_schedule_at()` | `<emb/work.h>` | no |
| `emb_event_attr_default()` | `<emb/event.h>` | no |
| `emb_event_clear()` | `<emb/event.h>` | no |
| `emb_event_get()` | `<emb/event.h>` | no |
| `emb_event_set()` | `<emb/event.h>` | no |
| `emb_fault()` | `<emb/fault.h>` | no |
| `emb_fault_hook()` | `<emb/fault.h>` | no |
| `emb_frame_decode_byte()` | `<emb/frame.h>` | no |
| `emb_frame_decoder_init()` | `<emb/frame.h>` | no |
| `emb_frame_encode()` | `<emb/frame.h>` | no |
| `emb_frame_write()` | `<emb/frame.h>` | no |
| `emb_in_isr()` | `<emb/context.h>` | no |
| `emb_instant_add()` | `<emb/time.h>` | no |
| `emb_instant_before()` | `<emb/time.h>` | no |
| `emb_instant_sub()` | `<emb/time.h>` | no |
| `emb_irq_clear_pending()` | `<emb/irq.h>` | no |
| `emb_irq_disable()` | `<emb/irq.h>` | no |
| `emb_irq_enable()` | `<emb/irq.h>` | no |
| `emb_irq_is_enabled()` | `<emb/irq.h>` | no |
| `emb_irq_is_locked()` | `<emb/irq.h>` | no |
| `emb_irq_lock()` | `<emb/irq.h>` | no |
| `emb_irq_nesting_depth()` | `<emb/irq.h>` | no |
| `emb_irq_pend()` | `<emb/irq.h>` | no |
| `emb_irq_unlock()` | `<emb/irq.h>` | no |
| `emb_isr_stack_info()` | `<emb/irq.h>` | no |
| `emb_log_flush()` | `<emb/log.h>` | no |
| `emb_log_format_check()` | `<emb/log.h>` | no |
| `emb_log_level_set()` | `<emb/log.h>` | no |
| `emb_log_stats_get()` | `<emb/log.h>` | no |
| `emb_log_write()` | `<emb/log.h>` | no |
| `emb_msgq_attr_default()` | `<emb/msgq.h>` | no |
| `emb_msgq_count()` | `<emb/msgq.h>` | no |
| `emb_msgq_free()` | `<emb/msgq.h>` | no |
| `emb_msgq_peek()` | `<emb/msgq.h>` | no |
| `emb_msgq_receive()` | `<emb/msgq.h>` | timeout |
| `emb_msgq_send()` | `<emb/msgq.h>` | timeout |
| `emb_msgq_send_front()` | `<emb/msgq.h>` | no |
| `emb_mul_sat_u64()` | `<emb/time.h>` | no |
| `emb_mutex_attr_default()` | `<emb/mutex.h>` | no |
| `emb_mutex_is_owner()` | `<emb/mutex.h>` | no |
| `emb_notify_set()` | `<emb/notify.h>` | no |
| `emb_ns_to_ticks()` | `<emb/time.h>` | no |
| `emb_sched_lock_depth()` | `<emb/sched.h>` | no |
| `emb_sem_attr_binary()` | `<emb/sem.h>` | no |
| `emb_sem_attr_default()` | `<emb/sem.h>` | no |
| `emb_sem_count()` | `<emb/sem.h>` | no |
| `emb_sem_give()` | `<emb/sem.h>` | no |
| `emb_status_name()` | `<emb/status.h>` | no |
| `emb_task_post()` | `<emb/task.h>` | no |
| `emb_task_post_event()` | `<emb/task.h>` | no |
| `emb_task_state()` | `<emb/task.h>` | no |
| `emb_task_stats_get()` | `<emb/task.h>` | no |
| `emb_thread_attr_default()` | `<emb/thread.h>` | no |
| `emb_thread_cancel()` | `<emb/thread.h>` | no |
| `emb_thread_detach()` | `<emb/thread.h>` | no |
| `emb_thread_get_effective_priority()` | `<emb/thread.h>` | no |
| `emb_thread_get_priority()` | `<emb/thread.h>` | no |
| `emb_thread_index()` | `<emb/thread.h>` | no |
| `emb_thread_name()` | `<emb/thread.h>` | no |
| `emb_thread_resume()` | `<emb/thread.h>` | no |
| `emb_thread_start()` | `<emb/thread.h>` | no |
| `emb_thread_state()` | `<emb/thread.h>` | no |
| `emb_ticks_at_least()` | `<emb/time.h>` | no |
| `emb_time_now()` | `<emb/time.h>` | no |
| `emb_timeout_from_duration()` | `<emb/time.h>` | no |
| `emb_timer_attr_default()` | `<emb/timer.h>` | no |
| `emb_timer_get_counts()` | `<emb/timer.h>` | no |
| `emb_timer_is_running()` | `<emb/timer.h>` | no |
| `emb_timer_remaining()` | `<emb/timer.h>` | no |
| `emb_timer_restart()` | `<emb/timer.h>` | no |
| `emb_timer_start()` | `<emb/timer.h>` | no |
| `emb_timer_start_at()` | `<emb/timer.h>` | no |
| `emb_timer_start_periodic()` | `<emb/timer.h>` | no |
| `emb_timer_stop()` | `<emb/timer.h>` | no |
| `emb_trace_dump()` | `<emb/trace.h>` | yes, in a thread: for the console |
| `emb_trace_stats_get()` | `<emb/trace.h>` | no |
| `emb_trace_stop()` | `<emb/trace.h>` | no |
| `emb_version_string()` | `<emb/version.h>` | no |
| `emb_wallclock_adjust()` | `<emb/wallclock.h>` | no |
| `emb_wallclock_get()` | `<emb/wallclock.h>` | no |
| `emb_wallclock_set()` | `<emb/wallclock.h>` | no |
| `emb_wdog_checkin()` | `<emb/wdog.h>` | no |
| `emb_work_cancel()` | `<emb/work.h>` | no |
| `emb_work_init()` | `<emb/work.h>` | no |
| `emb_work_state()` | `<emb/work.h>` | no |
| `emb_work_submit()` | `<emb/work.h>` | no |
| `emb_workq_attr_default()` | `<emb/work.h>` | no |
| `emb_workq_get_stats()` | `<emb/work.h>` | no |
| `emb_workq_reset_stats()` | `<emb/work.h>` | no |
| `emb_workq_system()` | `<emb/work.h>` | no |

<!-- isr-safe:end -->
