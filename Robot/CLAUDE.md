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

Every board port that enables SNTP or DNS must also define `LWIP_RAND()` in
its `lwipopts.h` — see the "Hidden heap use" note in the **No heap in Robot**
section below.

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

## Per-instance vs module-level callbacks

The "init + register" pattern above describes *module-level* observer lists —
one fixed-capacity array shared across all users of a module (e.g.
`ntp_on_sync(cb)`).

When a module manages multiple independent instances of a resource (e.g. SSE
channels in `net/http`), use a **per-instance callback field** instead:

```c
// In the Robot header, embed the hook directly in the instance struct:
typedef struct {
    struct tcp_pcb *pcb;        // managed by the engine
    void (*on_connect)(void);   // board supplies; called by engine on connect
} http_sse_chan_t;
```

The board sets the field before registering the instance:

```c
// In board init (http_streams.c):
status_chan.on_connect = status_sse_connected;
http_sse_bind("/status_stream", &status_chan);
```

The engine calls it without knowing what it does:

```c
if (ch->on_connect) ch->on_connect();
```

**When to use each form:**

| Form | Use when |
|------|----------|
| Module-level observer list | The event is global to the module (e.g. "NTP synced") and multiple boards may register different handlers. |
| Per-instance callback field | The event is specific to one instance of a resource (e.g. "this SSE channel just got a client") and each instance may need a different response. |

Mixing the two in one module is fine. The field may be NULL — always check
before calling.

## No heap in Robot

Robot modules must not allocate from the heap. This means:

- No `mem_malloc` / `malloc` / `calloc` — not even lwIP's `mem_malloc`.
- Connection pools and observer lists are **fixed-capacity static arrays**
  sized at compile time.
- The canary diagnostic (`Robot/diagnostics/canary`) will flag unexpected
  heap use. If the canary fires on code that should be heap-free, search for
  `mem_malloc` — it is easy to carry over from a board-layer prototype.

### Hidden heap use: lwIP SNTP and DNS call `rand()`

`sntp.c` and `dns.c` both call the C library `rand()`. Under newlib-nano,
`rand()` lazily allocates 24 bytes via `malloc` on its first call (to hold
reentrant state), which grows the heap by 32 bytes (24 data + 8-byte chunk
header). On a no-heap board this overwrites the canary region immediately
above BSS and the canary will report a 32-byte incursion.

**Fix required in every board port that enables SNTP or DNS:** define
`LWIP_RAND()` in `lwipopts.h` to point to a malloc-free PRNG implemented
in the board's lwIP port (e.g. `ethernetif.c`). A simple xorshift32 is
sufficient:

```c
/* in lwipopts.h */
extern unsigned int lwip_rand(void);
#define LWIP_RAND() lwip_rand()

/* in ethernetif.c */
u32_t lwip_rand(void) {
    static u32_t state = 0xDEADBEEFu;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
```

Without this override, `LWIP_RAND()` falls back to `rand()` (see
`Robot/net/lwip/src/include/lwip/opt.h`) and the canary will fire on the
first SNTP or DNS transaction.

The correct pattern for connection pools (established by `net/http`):

```c
static http_conn_t conns[HTTP_MAX_CONNECTIONS];  // fixed, in BSS

static http_conn_t *conn_alloc(void) {
    for (int i = 0; i < HTTP_MAX_CONNECTIONS; i++)
        if (conns[i].state == IDLE) return &conns[i];
    return NULL;
}

static void conn_free(http_conn_t *c) {
    c->state = IDLE;   /* slot returned to pool */
}
```

`conn_alloc` returns NULL when the pool is exhausted; the caller must handle
that (e.g. `tcp_abort` the new PCB).

## Splitting a board module into Robot engine + board application

When a board module is too large to move as a unit, split it:

- The **Robot engine** owns the generic mechanism (protocol, state machine,
  resource lifecycle). It exposes a registration API so boards can supply
  routes, callbacks, and data without the engine needing to know them.
- The **board application layer** owns all domain knowledge (JSON builders,
  hardware queries, specific routes). It calls the engine's registration API
  from a board-side `*_init()` function.

Naming: if the board application file would collide with the Robot engine
file (same header name on the flat include path), give the board file a
distinct name (e.g. `http_streams.h` alongside `http_server.h`) and update
all board-side callers. The Robot header is the canonical name; it wins.

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
