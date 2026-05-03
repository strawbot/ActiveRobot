# Project Context

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
