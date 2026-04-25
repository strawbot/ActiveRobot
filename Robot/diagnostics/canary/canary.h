// canary.h — Stack overflow detection via 0xDEADBEEF canary fill
//
// Part of the Robot/ shared layer.  Pure logic — depends only on TimbreOS's
// printers.h for output and on three linker symbols every adopting board must
// export:
//
//   _sstack          bottom of the system stack (top of free RAM, BSS end)
//   _estack          top of the system stack (initial SP)
//   _Min_Stack_Size  the developer's configured stack budget, in bytes
//
// Several STM32CubeMX-generated linker scripts already export _ebss but not
// _sstack.  The fix is one line in the .ld file:
//
//     _sstack = _ebss;
//
// added right after _ebss is defined.  See Robot/README.md for details.
//
// Usage:
//
//   // Once, as early as possible in main() — fills unused stack with the
//   // canary word.  Subsequent stack_check() calls measure how far the
//   // stack has grown.
//   stack_canary_init();
//
//   // Any time afterwards — typically from a CLI "show sys" command.
//   stack_info_t si = stack_check();
//   stack_render(si);   // prints a human-readable line via printers.h

#ifndef ROBOT_DIAGNOSTICS_CANARY_H
#define ROBOT_DIAGNOSTICS_CANARY_H

#include <stdint.h>

// ── Status ────────────────────────────────────────────────────────────────────

typedef enum {
    STACK_OK,        // canary intact at _sstack; hwm within _Min_Stack_Size
    STACK_WARN,      // canary intact; hwm > _Min_Stack_Size (used free RAM
                     // above the budget — no BSS corruption yet)
    STACK_OVERFLOW,  // no canary at _sstack; canary resumes higher up — stack
                     // reached into BSS; incursion depth and hwm both reported
    STACK_CRITICAL,  // no canary found from _sstack up to current SP — stack
                     // has consumed all free RAM; data may be corrupted
} stack_status_t;

typedef struct {
    stack_status_t status;
    uint32_t       hwm;        // bytes used from _estack downward (high-water mark)
    uint32_t       incursion;  // bytes below _sstack overwritten (OVERFLOW/CRITICAL)
    uint32_t       limit;      // _Min_Stack_Size — the configured budget
} stack_info_t;

// ── API ───────────────────────────────────────────────────────────────────────

// stack_canary_init — fill the unused stack region with 0xDEADBEEF.
// Call as early as possible in main(), before any deep call chain.
// Stops 64 bytes below the current SP for a safety margin.
void stack_canary_init(void);

// stack_check — single pass from _sstack toward _estack.
// Returns a populated stack_info_t.  Cheap (linear scan of free RAM); safe
// to call from any cooperative-task context.
stack_info_t stack_check(void);

// stack_render — print a one-line status to printers.h's emit queue.
// Format depends on status:
//   "OK  hwm=N/M B"
//   "WARNING  hwm=N/M B  exceeds _Min_Stack_Size"
//   "OVERFLOW  XB into BSS  hwm=N/M B"  (followed by a hex dump of the start)
//   "CRITICAL  stack consumed all free RAM  incursion>=XB"
// Caller is responsible for any leading "stack: " prefix and trailing newline.
void stack_render(stack_info_t info);

#endif // ROBOT_DIAGNOSTICS_CANARY_H
