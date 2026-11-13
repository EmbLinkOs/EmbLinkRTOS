# SPEC-020 - Tasks: Threads Written as State Machines

**Implementation and release scope:** [Project status](../STATUS.md) records the implemented subset; [roadmap](../architecture/07-roadmap.md) defines release gates. Nothing in this specification is implemented yet; the configurator phase implements it.

**Status:** Accepted 2026-11-13 by the project owner ("accept as written"), with the decisions of §10.
**Requirements:** `docs/requirements/KRN-TSK.md` (KRN-TSK-001 to 004); KRN-THR, KRN-IPC, KRN-TIM.
**Builds on:** SPEC-008 (threads), SPEC-005 §5 (message queues), SPEC-003 (time), SPEC-014 §5 (the system description, A125, A126).

---

## 1. Scope

A **task** is a thread whose body is a state machine: its states are declared in the system description, the generator writes the machine's tables and the thread that runs it, and the application writes one handler per state. Tasks are an application framework over the kernel's threads and queues, in the manner of MPLAB Harmony's `APP_Tasks()`. They add no scheduling of their own: a task is scheduled as the thread it is, by its priority.

Two models, chosen per task:
- **event**: the task waits on its own event queue and runs the current state's handler for each event. A task with nothing to do uses no processor time.
- **periodic**: the task runs the current state's handler every `period`, on a drift-free schedule, like Harmony's tasks called from a loop.

Plain threads stay available beside tasks, from the system description or from C.

## 2. In the system description

```yaml
tasks:
  - name: sensor
    model: event                  # or periodic
    priority: 5
    stack: 1K
    queue: 8                      # events the queue holds (event model)
    events: [tick, sample, fault] # this task's event ids
    states: [init, idle, measuring, error]
    initial: init
    entry: [measuring]            # states with an entry handler
    exit: [measuring]             # states with an exit handler
    timeout: { measuring: 100ms } # a state left with no event for this long gets EMB_EV_TIMEOUT
    data: sensor_data_t           # the task's own data, a C type; optional
    event: null                   # a C type replacing emb_task_event_t for this task; optional
  - name: blink
    model: periodic
    period: 500ms
    priority: 2
    stack: 512
    states: [on, off]
    initial: off
```

Every name is a C identifier, unique among the system's threads, objects, tasks and targets. A task's states, events, entry and exit lists name its own states and events, each once. `initial` is one of its states. `queue` and `events` belong to the event model, `period` to the periodic one. Its priority and stack are checked as a thread's are (A125).

## 3. Events

```c
typedef struct emb_task_event {
    uint32_t id;    /* an EMB_EV_* id below EMB_EV_USER, or the task's own */
    uint32_t value; /* the event's datum */
} emb_task_event_t;
```

- The ids below `EMB_EV_USER` (16) are the runtime's: `EMB_EV_ENTRY`, `EMB_EV_EXIT` and `EMB_EV_TIMEOUT`.
- A task's own events are numbered from `EMB_EV_USER` in the order the system lists them, as `<TASK>_EV_<EVENT>` (`SENSOR_EV_TICK`).
- An event is copied into the task's queue, and nothing points into the poster's memory.
- A task that declares `event:` has events of that type instead. Its first member must be a `uint32_t id`, which the generator checks with a static assertion.

## 4. States and handlers

The generator writes, for each task, `<TASK>_<STATE>` constants (`SENSOR_IDLE`), the task's type `<task>_t` (its runtime part, then its `data`), and declarations of the handlers the application defines:

```c
emb_state_t sensor_on_idle(sensor_t *self, const emb_task_event_t *ev); /* event model: per event */
emb_state_t blink_on_on(blink_t *self);                             /* periodic model: per period */
void sensor_enter_measuring(sensor_t *self);                        /* listed in entry: */
void sensor_exit_measuring(sensor_t *self);                         /* listed in exit: */
```

A handler returns the next state: its own state to stay, another to move. Moving runs the old state's exit handler, if it has one, then the new state's entry handler, if it has one, then re-arms the new state's timeout. The task starts by running its initial state's entry handler. A handler runs to completion: one event or one period at a time, no event lost while it runs, since posted events wait in the queue.

A handler the application has not written is a link error naming it. The declarations are generated.

## 5. The runtime

The generated thread's entry calls the runtime:

- **event model:** wait on the queue, for at most the current state's timeout if it has one. An event runs the state's handler, and a timeout runs it with `EMB_EV_TIMEOUT`.
- **periodic model:** wait until the next period's start (`emb_thread_sleep_until`), then run the state's handler. A period missed because a handler ran long is counted, not run twice.

```c
emb_status_t emb_task_post(emb_task_t *task, uint32_t id, uint32_t value);  /* thread or ISR; no wait */
emb_status_t emb_task_post_wait(emb_task_t *task, uint32_t id, uint32_t value, emb_timeout_t timeout);
emb_status_t emb_task_post_event(emb_task_t *task, const void *event);      /* a task's own event type */
emb_state_t  emb_task_state(const emb_task_t *task);
emb_status_t emb_task_stats_get(const emb_task_t *task, emb_task_stats_t *out); /* events, drops, timeouts, overruns */
```

- **Stopping a task:** cancel its thread (`emb_thread_cancel()`, SPEC-008 §7). The wait ends with `EMB_ECANCELED`, the current state's exit handler runs, and `emb_task_run()` returns, so the thread ends.
- **A full queue:** `emb_task_post()` from an ISR or with no wait returns `EMB_EAGAIN` and counts a drop. Events are never silently lost.
- **Handles:** each task is reached by its generated handle, `&sensor.task` or the macro `SENSOR_TASK`.
- **Calling contracts:** the API's tags follow SPEC-001 §9.

## 6. What is generated, and where

Under A126's layout:
- `generated/<board>/include/tasks.h`: the event and state constants, the task types, the handlers' declarations and the handles.
- `generated/<board>/src/tasks.c`: the queues, the dispatch tables, the threads (`EMB_THREAD_DEFINE`) and their entries.
- `src/tasks/<task>.c` and `src/tasks/<task>.h`: a skeleton with every handler written, returning its own state.

All three are editable, and regeneration merges (A126). Adding a state in `system.yaml` adds its constant and declaration, and merges a new handler into the task's file.

## 7. Configuration

`CONFIG_EMB_TASK` (default y in the base profile, n in tiny) builds the runtime, and needs `CONFIG_EMB_MSGQ` for the event model. The generator refuses tasks the configuration leaves out.

## 8. Verification

- **Native conformance:** an event task's transitions, entry and exit order, a state's timeout, a full queue counting its drop, posting from an ISR (the native port's simulated interrupt), a custom event type, a periodic task's drift-free schedule and its overrun count.
- **Generator tests:** each rule of §2, the generated tables, and a merge that adds a state's handler into an edited task file.
- **On hardware:** an example with an event task and a periodic task, on the NUCLEO-F446RE and the Pico 2 W.

## 9. Requirements (`KRN-TSK.md`)

- KRN-TSK-001: a task runs one handler at a time and never loses a posted event.
- KRN-TSK-002: a transition runs the exit handler, then the entry handler, then arms the timeout.
- KRN-TSK-003: a full queue returns an error and counts a drop.
- KRN-TSK-004: a periodic task's periods are drift-free, and a missed period is counted, not run late twice.

## 10. Decisions taken by the owner on 2026-11-13

- Tasks are threads declared as state machines, as in Harmony.
- Both models exist, chosen per task: event and periodic.
- An event is `{id, value}`, or a type the task declares.
- The specification is this new SPEC, together with A126 for the system description.
