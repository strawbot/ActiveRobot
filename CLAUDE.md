# ActiveRobot Project — Default Context

## Headers & APIs

| Module | Header | Purpose |
|--------|--------|---------|
| Operating System API | `tea.h` | Core OS functionality |
| Output / Printing | `printers.h` | All output and printing capabilities |
| Command Line Interface | `cli.h` | CLI interaction |

## Programming Philosophy

- **Simplicity** — prefer the straightforward solution over the clever one
- **Minimal** — include only what is necessary; avoid bloat
- **Responsive** — the system should remain reactive and not block

## System Model

- **Event-driven** — program flow is driven by events and actions
- **Cooperative multitasking** — long-running actions must yield and share computing time with other actions rather than monopolizing the processor

## Porting checklist — when adding a new board

### Build timestamp

`TimbreOS/clocks.c:print_build_banner()` reads `build_timestamp[]`, which is
declared `extern` and must be provided by a per-build generated file.  Without
it the linker will fail.

For each new board:

1. **Wire the generator as a pre-build step** — call
   `python3 ../../TimbreOS/gen_build_timestamp.py <output>` before compilation,
   where `<output>` is a `.c` file inside the board's compiled source tree:
   - CubeIDE (`.cproject`): append `&& python3 ../../TimbreOS/gen_build_timestamp.py ../Board/build_timestamp.c` to `prebuildStep`.
   - PlatformIO: add `pre:Scripts/gen_build_timestamp.py` to `extra_scripts` (see `board-NanoR4/Scripts/gen_build_timestamp.py` for the wrapper).
   - Makefile: add a `.PHONY: build_timestamp` target and depend `all` on it (see `board-Tiva/Makefile`).

2. **Ensure the generated file is compiled** — it should land in a directory
   already scanned by the build (e.g. `Board/` for CubeIDE boards, `src/` for
   PlatformIO/Makefile boards).

The file must be regenerated on every build (not just when something changed)
so that `__TIMESTAMP__` reflects the compiler's actual invocation time.
