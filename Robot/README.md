# Robot — Shared, board-independent code

`Robot/` holds code that all boards can use. Anything here is *board-agnostic*
and must not include board-specific headers, HAL headers, or pin maps.

## Layering rules

A module in `Robot/` may depend on:

1. **TimbreOS** (`tea.h`, `printers.h`, `cli.h`, `timeout.h`, etc.) — the OS.
2. **Common protocol stacks** that are themselves board-agnostic (currently lwIP).
3. **Other Robot modules** lower in the stack.

A module in `Robot/` must NOT depend on:

- Anything in `Discovery/`, `Nucleo411/`, `Nucleo446/`, `PNucleo/`, `Nano/`.
- STM32 HAL, Renesas FSP, or any vendor peripheral headers.
- Specific physical-medium drivers (Ethernet PHY, USB CDC, BLE, UART).

If a Robot module needs hardware-dependent behaviour, it exposes a *registration
hook* and lets the board supply the implementation at runtime (see below).

## Runtime registration (composition)

Boards compose Robot features by calling `init()` functions and registering
callbacks during start-up. There is no compile-time config menu — each board's
`main` explicitly opts in to what it needs. Modules follow this shape:

```c
// In a Robot module header:
void feature_init(void);
void feature_on_event(void (*cb)(void));   // observers register here
```

Keep observer lists small and fixed-capacity (no heap). The event-driven
cooperative multitasking model means callbacks must return quickly; anything
long-running should `later(fn)` into the action queue.

## Directory layout

```
Robot/
├── README.md               — this file
├── net/                    — board-agnostic networking features
│   ├── ntp/                — SNTP time sync (depends on lwIP only)
│   ├── telnet/             — Telnet server (lwIP raw TCP, port 23)
│   └── http/               — (future) HTTP server
├── diagnostics/            — health-monitoring and debug aids
│   └── canary/             — stack overflow detection (4-state OK/WARN/
│                              OVERFLOW/CRITICAL with high-water mark)
└── robot/                  — (future) limb sensing & control algorithms
```

## Linker-symbol contracts

Some Robot modules need linker-defined symbols.  Every adopting board's
`.ld` script must export them — usually a one-line addition.

| Module | Required symbols | If your linker only has _ebss |
|--------|------------------|-------------------------------|
| `diagnostics/canary` | `_sstack`, `_estack`, `_Min_Stack_Size` | Add `_sstack = _ebss;` immediately after `_ebss = .;` in the .bss section |

## Extracting a new feature into Robot/ — checklist

Using NTP as the worked example, the steps are:

1. Pick the cleanest existing implementation (Discovery's `ntp_sync.c`).
2. Identify every cross-feature call (`http_status_push()` was the only one).
3. Replace each direct call with a registration hook (`ntp_on_sync(cb)`).
4. Move `.c`/`.h` to `Robot/<area>/<feature>/`.
5. Add the include path and source to each adopting board's CMake file.
6. In each adopting board, register the old direct callers during `network_init`.
7. One-time per board: add `../Robot/` as a linked source folder in STM32CubeIDE
   so `subdir.mk` picks it up on regeneration.

## Adoption status

| Module | Discovery | Nucleo411 | Nucleo446 | PNucleo | Nano |
|--------|:---------:|:---------:|:---------:|:-------:|:----:|
| `net/ntp` | adopted | — | — | — | — |
| `net/telnet` | adopted | — | — | — | — |
| `diagnostics/canary` | adopted | adopted | adopted | adopted | adopted |

## Next candidates for extraction

In priority order:

1. HTTP server (largest surface; Telnet validates the raw TCP pattern).
2. Limb sensing / control (different domain — will need a sensor/actuator
   abstraction, not just a transport abstraction).
3. Adopt `net/ntp` and `net/telnet` on a second board (Nucleo446 or PNucleo)
   to mirror the canary cross-board validation.
