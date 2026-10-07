# embconfig

`embconfig.py` is the configuration tool of 05 §2.1: a small Python implementation of Kconfig semantics, with no dependency beyond PyYAML (for the port manifests).

```
python3 -I tools/kconfig/embconfig.py --kconfig Kconfig --manifest arch/avr/arch.yaml \
    --conf boards/arduino/arduino_uno/board.conf --conf configs/tiny.conf \
    --out-header build/generated/include/emb/config.h --out-cmake build/generated/config.cmake \
    --out-json build/generated/config.json --out-dotconfig build/.config
```

Inputs, in order of precedence (later wins): Kconfig defaults, `--conf` fragments in the order given (board, profile, extras), `--set SYMBOL=value`. The architecture manifest (SPEC-011 §2) seeds the read-only `EMB_ARCH*` and `EMB_ARCH_HAS_*` symbols; a fragment that tries to set one is an error.

## Supported Kconfig subset

`mainmenu`, `menu`/`endmenu`, `source "relative/path"`, `if <expr>`/`endif`, `config SYMBOL` with `bool`/`int`/`hex`/`string` (optionally followed by the prompt), `prompt`, `default <value> [if <expr>]` (a default may name another symbol of the same type), `depends on <expr>`, `range <lo> <hi> [if <expr>]` (bounds may be symbols), `select SYMBOL [if <expr>]`, `help` (indented block), `choice`/`endchoice` with `prompt`, `default`, `depends on`.

Expressions: `!`, `&&`, `||`, parentheses, `=`, `!=`, `<`, `>`, `<=`, `>=`, symbols, `y`, `n`, numbers, `"strings"`.

**Extension:** `require <expr> "message"` fails the configuration when the expression is false after resolution. Inside an `if` block it applies only when the block's condition holds. This is how a port refuses kernel options it cannot support (SPEC-011 §2, BLD-007) with a message that names the specification.

## Semantics

- A symbol with unmet dependencies takes its type default (`n`, `0`, `""`) and is still emitted (CODING-STANDARD CS-10.5: every boolean symbol is `0` or `1`); setting it in a fragment is an error that names the unsatisfied dependency and, for manifest-derived symbols, the manifest line.
- `select` forces a bool to `y`; selecting a symbol whose dependencies are unmet is an error.
- A `choice` has exactly one member `y`: the one set by a fragment, else the first applicable `default`, else the first member whose dependencies hold.
- Ranges are checked on the final value, whether it came from a fragment or a default.

## Outputs

| File | Content |
|---|---|
| `emb/config.h` | `#define CONFIG_EMB_X <value>` for every symbol; bools as `0`/`1`, strings quoted; plus `EMB_CONFIG_VERSION`, `EMB_CONFIG_HASH` (first 32 bits of the SHA-256 of `.config`) and `EMB_CONFIG_HASH_STRING` |
| `config.cmake` | `set(CONFIG_EMB_X "<value>")` for the build system |
| `config.json` | `{version, hash, symbols: {CONFIG_EMB_X: {type, value, origin}}}` for the generator and tools (05 §2.1) |
| `.config` | the resolved configuration in fragment syntax, reproducible input for the next run |

Files whose content did not change are not rewritten, so nothing rebuilds after a configure that resolved to the same values.
