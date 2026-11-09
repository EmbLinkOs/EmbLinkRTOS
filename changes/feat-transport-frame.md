### Added (framed byte streams, 2026-11-09)

- `<emb/frame.h>`, behind `CONFIG_EMB_FRAME` (on by default): `emb_frame_encode()` writes one frame, COBS over a channel byte, the payload and a CRC-16, ended by a zero; `emb_frame_decode_byte()` decodes a stream byte by byte, from a thread or an interrupt, drops a frame with a bad CRC or one longer than its buffer, counts both, and resynchronizes at the next zero (SPEC-015 §7, OBS-019). It is the framing M4.3's log, trace and crash channels and M4.5's update protocol run over. `tools/transport/frame.py` is the host's side: the same codec, and a tool that prints a serial port's or a capture's frames by channel.
- `subsys/` holds the subsystems over the kernel, built as `emb_subsys` and linked into every image, which keeps only what it calls.

### Changed (the frame's channels and CRC, 2026-11-09)

- A113, proposed: channels 0 to 4 are the system's (4 is the update protocol's, as SPEC-019 has it) and 5 to 15 the application's; the CRC is CRC-16/IBM-3740, sent least significant byte first ([amendments](docs/specs/AMENDMENTS-M4.md)).
