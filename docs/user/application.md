# Your own application

An application lives in its own directory, outside the RTOS's tree, with its own `CMakeLists.txt`. The RTOS is a CMake package: `find_package(EmbLinkRTOS)` locates it, picks the board's toolchain, and gives the application `emb_kernel()` and `emb_add_executable()`. [`examples/hello_app/`](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/examples/hello_app) is a complete one: a producer and a consumer passing readings through a message queue.

## The project

```text
my_app/
  CMakeLists.txt
  main.c
```

```cmake
cmake_minimum_required(VERSION 3.20)
find_package(EmbLinkRTOS REQUIRED)   # before project(): it picks the board's toolchain
project(my_app LANGUAGES C)
emb_kernel()                         # the kernel, the port and the board, for EMB_BOARD
emb_add_executable(app main.c)       # an image; also run_app where the board has an emulator
```

`main.c` initializes the board and the kernel, then starts it:

```c
#include <emb/emb.h>

#include <emb_board.h>

static void blink(void *arg)
{
    (void)arg;
    for (;;) {
        emb_board_led_toggle();
        (void)emb_thread_sleep(EMB_MS(500));
    }
}

EMB_THREAD_DEFINE(blink_thread, blink, NULL, 2u, 512u, 0u);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start(); /* never returns */
}
```

## Configure, build, run

Point CMake at the RTOS's `cmake/` directory and name the board:

```sh
cmake -S my_app -B build/my_app -G Ninja \
      -DEmbLinkRTOS_DIR=$HOME/EmbLinkRTOS/cmake -DEMB_BOARD=mps2_an386
cmake --build build/my_app
```

| `EMB_BOARD` | Toolchain the package picks | Run |
|---|---|---|
| `native` (default) | the host's C compiler | `./build/my_app/app` |
| `mps2_an385`, `mps2_an386`, `mps2_an500`, `mps2_an505`, `microbit` | `arm-none-eabi-gcc` | `cmake --build build/my_app --target run_app` (QEMU) |
| `riscv32_virt` | `riscv64-unknown-elf-gcc` with picolibc | `cmake --build build/my_app --target run_app` (QEMU) |
| `arduino_uno` | `avr-gcc` | flash `app.hex`: see [the Uno](../engineering/hil.md#the-arduino-uno-r3) |
| `esp32_devkitc` | Espressif's `xtensa-esp32-elf-gcc` (`xtensa-esp32-clang.cmake` for Espressif's Clang, `embcc-xtensa.cmake` for EmbCC) | flash `app.bin` at 0x1000: `embflash build/my_app/app.bin --esp serial:/dev/cu.usbserial-XXXX --base 0x1000 --verify`, or Espressif's `esptool.py write_flash 0x1000 build/my_app/app.bin` |
| `nucleo_f446re` | `arm-none-eabi-gcc` | flash `app.elf`: `openocd -f board/st_nucleo_f4.cfg -c "program build/my_app/app.elf verify reset exit"` |

`run_app` runs the image under the board's QEMU machine, its console on your terminal: press Ctrl-A then X to leave QEMU. `-DCMAKE_TOOLCHAIN_FILE=...` chooses another toolchain (ADR-053), and `EMB_PROFILE`, `EMB_CONF` and `EMB_SET` work as in the RTOS's own build ([configuration](configuration.md)): for example `-DEMB_SET="EMB_CHECKED=n"` for a release build. A board of your own lives in your project too: `-DEMB_BOARD_ROOT=boards_dir` ([board roots](../engineering/hwgen.md)).

Everything `emb_add_executable()` makes sits next to the image: on a board, `app.elf`, `app.hex`, `app.elf.map` and a size report, and, where the board boots one, `app.bin` (the ESP32 and ESP32-C3) or `app.uf2` (Pico 2 W).

## Threads, tasks and objects from a system description

Instead of writing `EMB_THREAD_DEFINE` and the other definition macros in C, describe the application in a `system.yaml`. It holds threads, tasks (threads written as state machines), kernel objects, device settings, SPI and I2C targets and the watchdog. The generator turns it into C and checks it before anything compiles (SPEC-014 §5, SPEC-020). `examples/tasks/system.yaml` is a complete one:

```yaml
schema_version: 0
kind: system
name: my_app
boards: [st/nucleo_f446re, rpi/pico2_w]
tasks:
  - name: door
    model: event                 # waits on its own queue
    priority: 5
    stack: 1K
    events: [open, close]
    states: [closed, open]
    initial: closed
    exit: [open]
    timeout: { open: 2s }        # no event for 2 s: the handler gets EMB_EV_TIMEOUT
  - name: blink
    model: periodic
    period: 500ms
    priority: 2
    stack: 512
    states: [dark, lit]
    initial: dark
```

```sh
python3 -I $HOME/EmbLinkRTOS/hw/gen/emb-hwgen.py generate --system system.yaml
```

This writes:

```text
my_app/
  system.yaml
  CMakeLists.txt              yours: generated once, editable
  src/main.c                  yours
  src/tasks/door.c, blink.c   yours: one handler per state, each returning the next state
  generated/
    .base/                    what the generator last wrote, for merging (commit it, do not edit)
    nucleo_f446re/            per board: config/ include/ src/ linker/ build/ docs/
```

- **Your code:** write each task's behaviour in its handlers, for example `emb_state_t door_on_open(door_t *self, const emb_task_event_t *ev)`. Post events with `emb_task_post(DOOR_TASK, DOOR_EV_OPEN, value)`, from a thread or an interrupt.
- **Every generated file is editable.** When you change `system.yaml` and generate again:
  - a file whose generated text did not change is left as it is;
  - a file you did not edit is rewritten;
  - a file you edited is merged with the change, as git merges.

  Only where you and the generator changed the same lines does `generate` report a conflict (`<<<<<<< yours` ... `>>>>>>> generated now`), and `check` fails until you resolve it.
- **Checked at generation**, each refusal naming its line:
  - priorities;
  - tasks' states, events and timeouts;
  - object types the configuration leaves out;
  - device options and baud rates;
  - SPI clocks, I2C addresses and the watchdog timeout.
- **After the link**, with a `stack-roots.json`, a stack below its worst case fails the build.
- **C definitions** still work beside the file. An application with hand-written sources sets `scaffold: false`, and the generator then writes only `generated/`.

## What the build checks

The configuration step runs the same checks as the RTOS's own build: the board's generated files must be current (HW-003), the configuration must be valid for the port, and the storage sizes come from the compiled layout probe. The same warnings apply to your sources (`-Wall -Wextra -Werror` and the rest, CODING-STANDARD CS-13). Pass `-DEMB_WERROR=OFF` to keep warnings from failing your build.

`-DEMB_LTO=ON` optimizes your code together with the kernel at link time (GCC and Clang, [ADR-056](../architecture/06-decision-records.md)). Kernel calls then inline into your threads. On the NUCLEO-F446RE a switch takes about a fifth fewer cycles and the image is about a fifth smaller. Stepping through such an image in a debugger is harder, so the option is off by default. If your own assembly or linker script references a C symbol, mark that symbol `EMB_USED`, or LTO removes it.
