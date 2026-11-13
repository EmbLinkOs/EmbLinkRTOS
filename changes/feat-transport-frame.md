### Added (framed byte streams, 2026-11-09)

- `<emb/frame.h>`, behind `CONFIG_EMB_FRAME` (on by default): `emb_frame_encode()` writes one frame, COBS over a channel byte, the payload and a CRC-16, ended by a zero; `emb_frame_decode_byte()` decodes a stream byte by byte, from a thread or an interrupt, drops a frame with a bad CRC or one longer than its buffer, counts both, and resynchronizes at the next zero (SPEC-015 §7, OBS-019). It is the framing M4.3's log, trace and crash channels and M4.5's update protocol run over. `tools/transport/frame.py` is the host's side: the same codec, and a tool that prints a serial port's or a capture's frames by channel.
- `subsys/` holds the subsystems over the kernel, built as `emb_subsys` and linked into every image, which keeps only what it calls.

### Changed (the frame's channels and CRC, 2026-11-09)

- A113, proposed: channels 0 to 4 are the system's (4 is the update protocol's, as SPEC-019 has it) and 5 to 15 the application's; the CRC is CRC-16/IBM-3740, sent least significant byte first ([amendments](docs/specs/AMENDMENTS-M4.md)).

### Added (logging, trace, the emb tool and the two-way relay, 2026-11-13)

- Logging (`<emb/log.h>`, A127): `EMB_LOG_ERR()` to `EMB_LOG_DBG()` with formats the compiler checks, packed in the caller into a ring that never waits and counts what it drops, sent by a log thread. Two backends share the record: plain text formatted on the board, and deferred, formatted on the host by `tools/logdec` from the image's ELF, whose call sites never reach flash.
- The console (`<emb/console.h>`): boards write raw bytes with `emb_board_write()`, `emb_board_puts()` is common, and `CONFIG_EMB_CONSOLE_FRAMED` puts everything on it in frames: text on channel 3, logs on 0, trace on 1.
- The trace ring (`emb_trace_start()`, `emb_trace_dump()`, A128): the kernel's events in RAM as 20-byte records, dumped or streamed on channel 1; `tools/trace` writes CTF 1.8, which Babeltrace 2 reads, and Perfetto's JSON.
- `scripts/emb` (`tools/emb`, BLD-005): `build`, `flash`, `monitor`, `hil` and `trace dump`.
- The NUCLEO relay works both ways and drives a reset, a power cut and a wake pin for the bench, on channel-15 frames.
- Examples: `logging`, `tracing`, `console_echo`; `tools/hil/log-soak` and `log_soak.py`, the framed console's soak.

### Changed

- A127 and A128, proposed; logging is not offered on AVR in M4.3.
