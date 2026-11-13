# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The system description (SPEC-014 §5, A125): a product's threads, objects, device settings,
targets and watchdog over a board of the tree. One valid system, then one broken rule per case,
each checking the system file, line and node path the error names (HW-002)."""
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from emb_hwgen import HwError, generate, system  # noqa: E402

GOOD = """\
schema_version: 0
kind: system
name: controller
board: st/nucleo_f446re
conf: [app.conf]
threads:
  - { name: control_thread, entry: control, priority: 6, stack: 1K }
  - { name: command_thread, entry: command, priority: 4, stack: 1K }
  - { name: telemetry_thread, entry: telemetry, priority: 2, stack: 1K, autostart: false }
objects:
  - { name: samples, type: msgq, item: sample_t, capacity: 8 }
  - { name: rx_bytes, type: msgq, item_size: 1, capacity: 64 }
  - { name: console, type: mutex, protocol: inherit }
  - { name: bus_lock, type: mutex, protocol: ceiling, users: [control_thread, command_thread] }
  - { name: ready, type: sem, initial: 0, max: 1 }
devices:
  usart2: { baud: 57600, rx_buffer: 128 }
spi_targets:
  - { name: flash_chip, bus: spi2, hz: 8MHz, mode: 3, cs: 0 }
i2c_targets:
  - { name: env, bus: i2c1, address: 0x76, speed: fast }
watchdog: { device: iwdg, timeout: 500ms }
"""


class Base(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="hwgen-system-"))
        self.addCleanup(shutil.rmtree, self.dir)
        (self.dir / "app.conf").write_text("# the application's fragment\n", encoding="utf-8")
        self.text = GOOD

    def edit(self, old, new):
        self.assertIn(old, self.text)
        self.text = self.text.replace(old, new, 1)

    def resolve(self):
        path = self.dir / "system.yaml"
        path.write_text(self.text, encoding="utf-8")
        return system.resolve(path, ROOT)

    def line_of(self, needle):
        for n, line in enumerate(self.text.splitlines(), 1):
            if needle in line:
                return n
        raise AssertionError(f"{needle!r} not in the system description")

    def expect(self, needle, path, message):
        with self.assertRaises(HwError) as cm:
            self.resolve()
        want = re.compile(rf"system\.yaml:{self.line_of(needle)}: {re.escape(path)}: .*{message}")
        hits = [e for e in cm.exception.errors if want.search(e)]
        self.assertTrue(hits, f"expected system.yaml:{self.line_of(needle)}: {path}: ...{message}, got:\n"
                        + "\n".join(cm.exception.errors))


class Valid(Base):
    def test_resolves_with_the_board(self):
        r = self.resolve()
        s = r["system"]
        self.assertEqual([t["name"] for t in s["threads"]], ["control_thread", "command_thread", "telemetry_thread"])
        self.assertEqual(s["threads"][2]["flags"], ["EMB_THREAD_NO_AUTOSTART"])
        lock = next(o for o in s["objects"] if o["name"] == "bus_lock")
        self.assertEqual(lock["ceiling"], 6)  # the highest of its users: larger is higher (SPEC-008)
        self.assertEqual(s["spi_targets"][0]["hz"], 8_000_000)
        self.assertEqual(s["i2c_targets"][0]["hz"], 400_000)
        self.assertEqual(s["watchdog"]["timeout_ms"], 500)
        self.assertEqual(s["config"]["CONFIG_EMB_PRIORITY_COUNT"], 32)

    def test_device_settings_replace_the_boards(self):
        r = self.resolve()
        usart2 = next(d for d in r["devices"] if d["name"] == "usart2")
        self.assertEqual(next(f["value"] for f in usart2["fields"] if f["field"] == "baud"), 57600)
        self.assertEqual(next(b["size"] for b in usart2["buffers"] if b["name"] == "rx"), 128)
        self.assertEqual(r["console"]["baud"], 57600)  # the console is usart2: it follows
        self.assertEqual(r["system"]["devices"]["usart2"]["baud"], {"value": 57600, "board": 115200})

    def test_inputs_stamp_the_system_and_its_fragments(self):
        names = {Path(i["path"]).name for i in self.resolve()["inputs"]}
        self.assertTrue({"system.yaml", "app.conf", "board.yaml"} <= names, names)

    def test_a_board_beside_the_system(self):
        board = self.dir / "boards" / "acme_m4"
        board.mkdir(parents=True)
        text = (ROOT / "boards/arm/mps2_an386/board.yaml").read_text(encoding="utf-8")
        (board / "board.yaml").write_text(text.replace("name: mps2_an386", "name: acme_m4"), encoding="utf-8")
        self.text = "\n".join(l for l in GOOD.splitlines() if not re.match(r"^(  - \{ name: (flash_chip|env)|spi_targets|i2c_targets|watchdog|devices|  usart2)", l))
        self.text = self.text.replace("board: st/nucleo_f446re", "board: boards/acme_m4") + "\n"
        self.assertEqual(self.resolve()["board"]["name"], "acme_m4")


MINIMAL = """\
schema_version: 0
kind: system
name: blinky
boards: [st/nucleo_f446re, st/nucleo_f446ze]
threads:
  - { name: blink_thread, entry: blink, priority: 3, stack: 512 }
objects:
  - { name: ticks, type: sem, initial: 0, max: 10 }
"""


class SeveralBoards(Base):
    def test_each_board_resolved_and_generated(self):
        self.text = MINIMAL
        path = self.dir / "system.yaml"
        path.write_text(self.text, encoding="utf-8")
        self.assertEqual(system.boards(path, ROOT), ["st/nucleo_f446re", "st/nucleo_f446ze"])
        self.assertEqual(system.resolve(path, ROOT, "st/nucleo_f446ze")["board"]["name"], "nucleo_f446ze")
        per_board, _ = generate.generate_system(path, ROOT)
        self.assertEqual(sorted(per_board), ["_app", "nucleo_f446re", "nucleo_f446ze"])  # and the application's files
        for name, files in per_board.items():
            if name == "_app":
                continue
            self.assertIn("EMB_THREAD_DEFINE(blink_thread, blink, NULL, 3u, 512u, 0u);", files["src/system.c"])
            self.assertIn("EMB_SEM_DEFINE(ticks, 0u, 10u);", files["src/system.c"])
            self.assertIn(f'"board": "{name}"', files["docs/system.json"])

    def test_an_error_names_its_board(self):
        self.text = MINIMAL.replace("    max: 10", "    max: 10") + "devices:\n  usart2: { baud: 57600 }\n"
        with self.assertRaises(HwError) as cm:
            path = self.dir / "system.yaml"
            path.write_text(self.text, encoding="utf-8")
            generate.generate_system(path, ROOT)
        self.assertTrue(any("nucleo_f446ze has no device 'usart2'" in e for e in cm.exception.errors), cm.exception.errors)
        self.assertFalse(any("nucleo_f446re has no device" in e for e in cm.exception.errors))

    def test_board_and_boards_both(self):
        self.text = MINIMAL.replace("boards: [", "board: st/nucleo_f446re\nboards: [")
        with self.assertRaises(HwError) as cm:
            self.resolve()
        self.assertTrue(any("one of them" in e for e in cm.exception.errors), cm.exception.errors)


class Outputs(Base):
    def test_system_c_and_h(self):
        self.text = self.text.replace("conf: [app.conf]", "conf: [app.conf]\nincludes: [app.h]")
        path = self.dir / "system.yaml"
        path.write_text(self.text, encoding="utf-8")
        files = generate.generate_system(path, ROOT)[0]["nucleo_f446re"]
        c, h = files["src/system.c"], files["include/system.h"]
        self.assertIn("EMB_THREAD_DEFINE(telemetry_thread, telemetry, NULL, 2u, 1024u, EMB_THREAD_NO_AUTOSTART);", c)
        self.assertIn("EMB_MSGQ_DEFINE(samples, sizeof(sample_t), 8u, 0u);", c)
        self.assertIn("EMB_MUTEX_DEFINE(bus_lock, EMB_MUTEX_CEILING, 6u, 0u);", c)
        self.assertIn("const emb_drv_spi_target_t flash_chip = {8000000u, EMB_DRV_SPI_MODE_3, 8u, 0u, 0u, EMB_SOC_SPI2_CS0};", c)
        self.assertIn("const emb_drv_i2c_target_t env = {EMB_DRV_I2C_FAST, 0x76u, {0u, 0u}};", c)
        self.assertIn("void control(void *arg);", h)
        self.assertIn("extern emb_mutex_t bus_lock;", h)
        self.assertIn('#include "app.h"', h)
        self.assertIn("#define EMB_SYSTEM_WATCHDOG_TIMEOUT_MS 500u", h)
        self.assertIn("generate --system <path to>/", c)  # no path of this machine (HW-013)
        self.assertIn("| usart2 | baud | 57600 | 115200 |", files["docs/system.md"])
        # the board's outputs carry the system's settings
        self.assertIn("57600", files["include/hw_config.h"])


class Regenerate(Base):
    """A126: an edited generated file survives regeneration, merged with the generator's change;
    a change to the same lines is a conflict that check refuses."""

    def cli(self, *argv):
        import contextlib
        import importlib.util
        import io
        spec = importlib.util.spec_from_file_location("emb_hwgen_cli", HERE / "emb-hwgen.py")
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            code = cli.main(list(argv))
        return code, out.getvalue()

    def test_an_edit_survives_a_change_of_the_description(self):
        self.text = MINIMAL.replace("boards: [st/nucleo_f446re, st/nucleo_f446ze]", "board: st/nucleo_f446re")
        path = self.dir / "system.yaml"
        path.write_text(self.text, encoding="utf-8")
        self.assertEqual(self.cli("generate", "--system", str(path))[0], 0)
        c = self.dir / "generated/nucleo_f446re/src/system.c"
        self.assertTrue((self.dir / "generated/.base/nucleo_f446re/src/system.c").is_file())
        c.write_text(c.read_text(encoding="utf-8").replace('#include "system.h"\n', '#include "system.h"\n/* my note */\n'),
                     encoding="utf-8")
        # the description changes: one more object
        path.write_text(self.text + "  - { name: done, type: sem, initial: 0, max: 1 }\n", encoding="utf-8")
        code, out = self.cli("generate", "--system", str(path))
        self.assertEqual(code, 0, out)
        self.assertIn("merged", out)
        text = c.read_text(encoding="utf-8")
        self.assertIn("/* my note */", text)                    # the edit stayed
        self.assertIn("EMB_SEM_DEFINE(done, 0u, 1u);", text)   # and the change came in

    def test_a_conflict_is_reported_and_refused(self):
        self.text = MINIMAL.replace("boards: [st/nucleo_f446re, st/nucleo_f446ze]", "board: st/nucleo_f446re")
        path = self.dir / "system.yaml"
        path.write_text(self.text, encoding="utf-8")
        self.cli("generate", "--system", str(path))
        c = self.dir / "generated/nucleo_f446re/src/system.c"
        c.write_text(c.read_text(encoding="utf-8").replace("3u, 512u", "3u, 600u"), encoding="utf-8")
        path.write_text(self.text.replace("stack: 512", "stack: 768"), encoding="utf-8")
        code, out = self.cli("generate", "--system", str(path))
        self.assertEqual(code, 1)
        self.assertIn("conflict", out)
        from emb_hwgen import writer
        self.assertTrue(writer.has_conflict(c.read_text(encoding="utf-8")))


TASKS = """\
schema_version: 0
kind: system
name: tasks_demo
board: st/nucleo_f446re
tasks:
  - name: door
    priority: 5
    stack: 1K
    events: [open, close]
    states: [closed, open]
    initial: closed
    entry: [open]
    exit: [open]
    timeout: { open: 2s }
    data: door_data_t
  - name: blink
    model: periodic
    period: 500ms
    priority: 2
    stack: 512
    states: [dark, lit]
    initial: dark
"""


class Tasks(Base):
    """SPEC-020 in the system description: the checks, tasks.h and tasks.c, and the application's
    own files (A126)."""

    def setUp(self):
        super().setUp()
        self.text = TASKS

    def generate(self):
        path = self.dir / "system.yaml"
        path.write_text(self.text, encoding="utf-8")
        return generate.generate_system(path, ROOT)[0]

    def test_tables_and_threads(self):
        files = self.generate()
        h, c = files["nucleo_f446re"]["include/tasks.h"], files["nucleo_f446re"]["src/tasks.c"]
        self.assertIn("#define DOOR_OPEN ((emb_state_t)1u)", h)
        self.assertIn("#define DOOR_EV_CLOSE (EMB_EV_USER + 1u)", h)
        self.assertIn("door_data_t data;", h)
        self.assertIn("emb_state_t door_on_open(door_t *self, const emb_task_event_t *ev);", h)
        self.assertIn("emb_state_t blink_on_lit(blink_t *self);", h)
        self.assertIn("void door_exit_open(door_t *self);", h)
        self.assertIn('#include "tasks/door.h"', h)
        self.assertIn("static const uint32_t door_timeouts[2] = {0u, 2000u};", c)
        self.assertIn("EMB_THREAD_DEFINE(blink_thread, blink_main, NULL, 2u, 512u, 0u);", c)
        self.assertIn("next = door_on_open(&door, &door_event);", c)
        self.assertIn('#include "tasks.h"', files["nucleo_f446re"]["include/system.h"])

    def test_the_application_files(self):
        app = self.generate()["_app"]
        self.assertEqual(sorted(app), ["CMakeLists.txt", "src/main.c", "src/tasks/blink.c", "src/tasks/door.c",
                                       "src/tasks/door.h"])
        self.assertIn("emb_add_executable(tasks_demo src/main.c src/tasks/door.c src/tasks/blink.c)", app["CMakeLists.txt"])
        self.assertIn('set(EMB_SYSTEM "${CMAKE_CURRENT_LIST_DIR}/system.yaml"', app["CMakeLists.txt"])
        self.assertIn("$ENV{EMBLINKRTOS}/cmake", app["CMakeLists.txt"])  # outside the tree: no machine path
        self.assertIn("emb_kernel_start();", app["src/main.c"])
        self.assertIn("return DOOR_OPEN;", app["src/tasks/door.c"])
        self.assertIn("door_data_t;", app["src/tasks/door.h"])

    def test_no_scaffold(self):
        self.text = TASKS.replace("board: st/nucleo_f446re", "board: st/nucleo_f446re\nscaffold: false")
        self.assertNotIn("_app", self.generate())

    def expect_task(self, edit, needle, path, message):
        self.text = TASKS.replace(*edit)
        self.expect(needle, path, message)

    def test_initial_not_a_state(self):
        self.expect_task(("initial: closed", "initial: ajar"), "initial: ajar", "tasks.door.initial", "not one of its states")

    def test_entry_not_a_state(self):
        self.expect_task(("entry: [open]", "entry: [ajar]"), "entry: [ajar]", "tasks.door.entry", "not one of its states")

    def test_periodic_takes_no_events(self):
        self.expect_task(("    period: 500ms\n", "    period: 500ms\n    events: [x]\n"), "events: [x]",
                         "tasks.blink.events", "a periodic task takes no events")

    def test_event_task_takes_no_period(self):
        self.expect_task(("    timeout: { open: 2s }\n", "    timeout: { open: 2s }\n    period: 1s\n"), "period: 1s",
                         "tasks.door.period", "an event task takes no period")

    def test_needs_the_option(self):
        (self.dir / "app.conf").write_text("CONFIG_EMB_TASK=n\n", encoding="utf-8")
        self.expect_task(("board: st/nucleo_f446re", "board: st/nucleo_f446re\nconf: [app.conf]"), "name: door",
                         "tasks.door", "needs CONFIG_EMB_TASK")

    def test_yaml_booleans_are_explained(self):
        self.expect_task(("states: [dark, lit]\n    initial: dark", "states: [off, on]\n    initial: off"),
                         "states: [off, on]", "tasks.blink.states[0]", "quote the name")


class Negative(Base):
    def test_unknown_key(self):
        self.edit("name: controller", "name: controller\nflavour: lemon")
        self.expect("flavour", "flavour", "unknown key")

    def test_no_such_board(self):
        self.edit("board: st/nucleo_f446re", "board: st/nucleo_f999")
        self.expect("board:", "board", "no board 'st/nucleo_f999'")

    def test_missing_fragment(self):
        self.edit("conf: [app.conf]", "conf: [missing.conf]")
        self.expect("conf:", "conf[0]", "no such file missing.conf")

    def test_priority_zero_is_idle(self):
        self.edit("priority: 2, stack: 1K, autostart", "priority: 0, stack: 1K, autostart")
        self.expect("telemetry_thread", "threads.telemetry_thread.priority", "idle level")

    def test_priority_above_the_count(self):
        self.edit("priority: 6", "priority: 32")
        self.expect("control_thread, entry", "threads.control_thread.priority", "outside 1 to 31")

    def test_stack_below_the_ports_minimum(self):
        self.edit("priority: 4, stack: 1K", "priority: 4, stack: 128")
        self.expect("command_thread, entry", "threads.command_thread.stack", "below the port's minimum stack of 256")

    def test_object_type_left_out_of_the_configuration(self):
        (self.dir / "app.conf").write_text("CONFIG_EMB_MSGQ=n\n", encoding="utf-8")
        self.expect("name: samples", "objects.samples.type", "needs CONFIG_EMB_MSGQ")

    def test_option_of_another_type(self):
        self.edit("type: sem, initial: 0, max: 1", "type: sem, initial: 0, max: 1, capacity: 4")
        self.expect("name: ready", "objects.ready.capacity", "a sem takes no 'capacity'")

    def test_ceiling_user_not_a_thread(self):
        self.edit("users: [control_thread, command_thread]", "users: [control_thread, ghost]")
        self.expect("name: bus_lock", "objects.bus_lock.users", "'ghost' is not a thread")

    def test_msgq_item_and_item_size(self):
        self.edit("item: sample_t, capacity: 8", "item: sample_t, item_size: 4, capacity: 8")
        self.expect("name: samples", "objects.samples", "an item .* or an item_size")

    def test_duplicate_name_across_sections(self):
        self.edit("name: ready, type: sem", "name: console_lock, type: sem")
        self.edit("name: env, bus: i2c1", "name: console_lock, bus: i2c1")
        self.expect("name: console_lock, bus", "i2c_targets.console_lock", "already the name of objects.console_lock")

    def test_device_not_on_the_board(self):
        self.edit("usart2: { baud: 57600, rx_buffer: 128 }", "usart9: { baud: 57600 }")
        self.expect("usart9", "devices.usart9", "has no device 'usart9'")

    def test_option_the_binding_does_not_take(self):
        self.edit("baud: 57600, rx_buffer: 128", "baud: 57600, parity: odd")
        self.expect("usart2:", "devices.usart2.parity", "takes no option 'parity'")

    def test_wiring_is_the_boards(self):
        self.edit("baud: 57600, rx_buffer: 128", "baud: 57600, dma: tx")
        self.expect("usart2:", "devices.usart2.dma", "the board's wiring")

    def test_buffer_size_located_in_the_system(self):
        self.edit("rx_buffer: 128", "rx_buffer: 0")
        self.expect("usart2:", "devices.usart2.rx_buffer", "needs a size of 1 to 65535")

    def test_baud_the_clock_cannot_make(self):
        self.edit("baud: 57600", "baud: 2900000")
        self.expect("usart2:", "devices.usart2.baud", "not within 2 %")

    def test_spi_target_too_slow(self):
        self.edit("hz: 8MHz", "hz: 100Hz")
        self.expect("name: flash_chip", "spi_targets.flash_chip.hz", "below the slowest clock spi2 makes")

    def test_spi_target_chip_select_out_of_range(self):
        self.edit("mode: 3, cs: 0", "mode: 3, cs: 1")
        self.expect("name: flash_chip", "spi_targets.flash_chip.cs", "has 1 chip select")

    def test_spi_targets_sharing_a_chip_select(self):
        self.edit("  - { name: flash_chip, bus: spi2, hz: 8MHz, mode: 3, cs: 0 }",
                  "  - { name: flash_chip, bus: spi2, hz: 8MHz, mode: 3, cs: 0 }\n  - { name: other, bus: spi2, hz: 1MHz, cs: 0 }")
        self.expect("name: other", "spi_targets.other.cs", "already flash_chip's")

    def test_spi_target_on_an_i2c_bus(self):
        self.edit("bus: spi2", "bus: i2c1")
        self.expect("name: flash_chip", "spi_targets.flash_chip.bus", "i2c device, not an SPI controller")

    def test_i2c_speed_the_controller_lacks(self):
        self.edit("speed: fast", "speed: fast_plus")
        self.expect("name: env", "i2c_targets.env.speed", "runs at standard, fast, not fast_plus")

    def test_i2c_address_taken(self):
        self.edit("  - { name: env, bus: i2c1, address: 0x76, speed: fast }",
                  "  - { name: env, bus: i2c1, address: 0x76, speed: fast }\n  - { name: env2, bus: i2c1, address: 0x76 }")
        self.expect("name: env2", "i2c_targets.env2.address", "already env's")

    def test_watchdog_timeout_out_of_range(self):
        self.edit("timeout: 500ms", "timeout: 30s")
        self.expect("watchdog:", "watchdog.timeout", "30000 ms is outside iwdg's 1 to 22304 ms")

    def test_watchdog_not_a_watchdog(self):
        self.edit("device: iwdg", "device: rtc")
        self.expect("watchdog:", "watchdog.device", "rtc is a rtc device, not a watchdog")


if __name__ == "__main__":
    unittest.main()
