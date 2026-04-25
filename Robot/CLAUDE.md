# Robot/ — Binding rules for Claude

Anything inside `Robot/` is shared, board-agnostic code. These rules are
authoritative. `Robot/README.md` is the human-facing narrative; this file is
what Claude must follow when adding, editing, or reviewing code here.

See the top-level project instructions for the overall programming
philosophy (simplicity, minimal, responsive) and system model (event-driven,
cooperative multitasking). The rules below refine that for `Robot/`.

## The one-way dependency rule

Dependency arrows point one way only:

    board (Nano, Discovery, Nucleo411, Nucleo446, PNucleo, TIVA, ...)
      └──► Robot
             └──► TimbreOS (tea.h, printers.h, cli.h, timeout.h, ...)
             └──► board-agnostic protocol sources (e.g. lwIP core)

A file in `Robot/` may include:

1. TimbreOS headers.
2. Board-agnostic protocol sources that live inside `Robot/` itself.
3. Other `Robot/` modules lower in the stack.

A file in `Robot/` must NEVER include:

- Anything from `Discovery/`, `Nucleo411/`, `Nucleo446/`, `PNucleo/`, `Nano/`,
  `TIVA/`, or any other board folder.
- STM32 HAL (`stm32*_hal.h`), Renesas FSP (`r_*.h`, `hal_data.h`), TivaWare,
  or any vendor peripheral headers.
- Specific physical-medium drivers (Ethernet PHY, USB CDC, BLE, UART,
  `ethernetif.h`, `lan8742.h`, etc.).
- Pin maps, board-specific linker symbols that are not declared as contracts
  in this folder's `README.md`, or anything derived from a `.ioc` /
  `configuration.xml`.

No `#ifdef BOARD_*` in Robot. If a module needs hardware-flavored behavior,
it exposes a registration hook (see next section). That is the *only*
permitted way for a board to customize Robot behavior.

## The init + register + respond pattern

Every Robot module follows this shape:

```c
// Robot/<area>/<feature>/feature.h
void feature_init(void);                         // idempotent, cheap
void feature_on_event(void (*cb)(void));         // fixed-capacity observer
void feature_bind_hw(const feature_hw_t *ops);   // board supplies HW shim
```

Rules that apply to every module here:

- `feature_init()` must be safe to call exactly once from a board's `main`.
- Observer lists are fixed-capacity, no heap. Register at startup, not per
  event.
- Callbacks must return quickly. Long work goes on the action queue via
  `later(fn)` (see `tea.h`). Nothing in `Robot/` may busy-wait or block.
- All output goes through `printers.h`. Do not call `printf`, `puts`,
  `HAL_UART_Transmit`, or anything equivalent.
- If a feature needs hardware access, it takes a const-struct of function
  pointers that the board supplies (`feature_bind_hw`). The struct lives in
  the Robot header; the implementation lives in the board.

## lwIP — where the source lives

lwIP splits into two parts with opposite ownership:

- **Protocol core** (`src/core/`, `src/api/`, `src/netif/` minus ports) is
  pure C and board-agnostic. It belongs **once**, under `Robot/net/lwip/`,
  as a single upstream drop. Do not duplicate it into any board folder.
  Boards add the Robot lwIP sources to their compile list via their own
  build system (CMake for STM32, RA/PlatformIO for Nano, etc.).

- **Port layer** — `lwipopts.h`, `sys_arch.c/h`, `cc.h`, `ethernetif.c/h`,
  and the MAC/PHY glue — is inherently board-specific and stays in each
  board (typically `<Board>/LWIP/` or `<Board>/Middlewares/lwIP/`). The port
  must never be copied into `Robot/`.

When factoring the existing duplicated trees out of `Discovery/LWIP/` and
`Nucleo446/Middlewares/lwIP/`:

1. Diff the two protocol cores. If they are the same upstream version,
   move one copy to `Robot/net/lwip/` and delete both board copies of the
   core sources.
2. Leave each board's `lwipopts.h` and port files alone.
3. Update each board's CMake (or equivalent) to add the Robot lwIP include
   path and source list; remove the now-deleted per-board core sources from
   that board's build.
4. If the two boards' core versions differ, resolve to the newer one in
   Robot, then fix up any port-layer calls that moved or changed signature.
   Do not keep divergent lwIP cores.

Any new lwIP-using feature (Telnet, HTTP, etc.) lives in `Robot/net/<feature>/`
and depends only on the lwIP API — never on the board's port directly.

## CLI words and common diagnostics

Diagnostics that ask only TimbreOS/Robot questions belong in `Robot/`:

- `show-time`, `show-sys`, `show-timers`, `show-canary`, `show-net`, etc.
- Anything whose answer is "ask the OS or a Robot module, format with
  `printers.h`."

Each such command lives in a small module under `Robot/diagnostics/` (either
one file per command or grouped by subject) and **self-registers** with
`cli.h` inside its own `init()`:

```c
// Robot/diagnostics/show_time/show_time.c
static int cmd_show_time(int argc, char **argv) { /* ... */ }

void show_time_init(void) {
    cli_register("show-time", "print current system time", cmd_show_time);
}
```

Consequences:

- There is **no central table** of CLI commands anywhere in the codebase.
- The set of commands a given board exposes is exactly the set of
  diagnostic modules it compiles and whose `init()` it calls from `main`.
- A command a board chooses not to include simply does not exist on that
  board's CLI.

Board-specific diagnostic words (e.g. `show-eth` that reads the STM32 MAC,
`show-pins` that dumps a Renesas port) stay in the board folder and use the
same `cli_register(...)` API. They must not be added to Robot.

## When adding a new feature to Robot

Use the canary and NTP modules as templates. Before moving code into
`Robot/`:

1. Confirm it compiles against TimbreOS + lwIP only — no vendor headers.
2. Replace every direct cross-feature call with a registration hook.
3. Replace every direct hardware poke with a `*_bind_hw()` shim supplied
   by the board.
4. Place under `Robot/<area>/<feature>/` with a `feature.h` / `feature.c`.
5. Update the Adoption-status table in `Robot/README.md`.
6. Each adopting board adds the source to its build and calls
   `feature_init()` (plus any registrations) from `main`.

## When editing existing Robot code

- Do not introduce new `#include` lines that reach outside `Robot/` or
  TimbreOS. If you feel the need, you are about to break the layering; add
  a registration hook instead.
- Do not grow a module's API surface just to satisfy one board. Board-
  specific behavior lives in the board.
- Preserve the "callbacks return quickly, long work goes on `later(fn)`"
  contract.
- Preserve fixed-capacity observer arrays. Do not introduce heap.

## What does NOT belong in Robot/

- Startup code, linker scripts, `.ioc` / `configuration.xml`, pin maps.
- FreeRTOS/CMSIS/HAL shims. Robot runs on TimbreOS only.
- USB, Ethernet, UART, I2C, SPI *drivers*. Robot may define an interface
  (a struct of function pointers) that a driver satisfies, but the driver
  itself lives in the board.
- Any code that would only ever be used on one specific board. If it would,
  leave it in that board.
