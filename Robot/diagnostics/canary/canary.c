// canary.c — Stack overflow detection (board-independent)
//
// Part of the Robot/ shared layer.  Extracted from Nucleo446/Board/nucleo_cli.c
// (the most sophisticated of three near-duplicates that lived under
// Discovery/Board, Nucleo411/Board, and Nucleo446/Board).
//
// Two changes relative to the source:
//
//   1. The CLI rendering that was hard-coded inside the Nucleo446 show_sys()
//      lives here as stack_render(), so every board's CLI just calls
//      stack_render(stack_check()) without re-implementing the format.
//
//   2. stack_check() is now non-static so other Robot modules (or future
//      health-monitor tasks) can poll it programmatically without going
//      through the CLI.
//
// See canary.h for the linker-symbol contract every adopting board must meet.

#include "canary.h"
#include "printers.h"

// ── Linker-provided symbols ───────────────────────────────────────────────────
// Every adopting board must export these three.  See canary.h for guidance
// on boards whose .ld script exports _ebss but not _sstack.

extern uint32_t _sstack;          // bottom of free RAM (end of BSS)
extern uint32_t _estack;          // top of RAM / initial SP
extern uint32_t _Min_Stack_Size;  // configured stack budget (bytes)

// ── Constants ─────────────────────────────────────────────────────────────────

#define CANARY_WORD  0xDEADBEEFu

// Safety margin above the current SP — we don't want to write canary words
// over the live stack frame of stack_canary_init() itself.
#define INIT_SP_MARGIN_BYTES  64u

// ── Public API ────────────────────────────────────────────────────────────────

void stack_canary_init(void)
{
    uint32_t sp;
    __asm volatile ("mov %0, sp" : "=r" (sp));
    volatile uint32_t *p     = (volatile uint32_t *)&_sstack;
    volatile uint32_t *limit = (volatile uint32_t *)(sp - INIT_SP_MARGIN_BYTES);
    while (p < limit) *p++ = CANARY_WORD;
}

stack_info_t stack_check(void)
{
    stack_info_t si;
    si.limit     = (uint32_t)&_Min_Stack_Size;
    si.incursion = 0;

    volatile uint32_t *bot = (volatile uint32_t *)&_sstack;
    volatile uint32_t *top = (volatile uint32_t *)&_estack;
    volatile uint32_t *p   = bot;

    if (p[0] == CANARY_WORD && p[1] == CANARY_WORD) {
        // Bottom intact — scan up to find first non-canary word = HWM.
        while (p < top && *p == CANARY_WORD) p++;
        si.hwm    = (uint32_t)((uintptr_t)top - (uintptr_t)p);
        si.status = (si.hwm > si.limit) ? STACK_WARN : STACK_OK;
    } else {
        // No canary at bottom — scan up to first canary or current SP.
        p++; // skip first word, which is already non-canary
        uint32_t sp_val;
        __asm volatile ("mov %0, sp" : "=r" (sp_val));
        volatile uint32_t *sp_ptr = (volatile uint32_t *)sp_val;

        while (p < sp_ptr && *p != CANARY_WORD) p++;

        if (p >= sp_ptr) {
            // No canary found up to SP — stack consumed all free RAM.
            si.incursion = (uint32_t)((uintptr_t)sp_ptr - (uintptr_t)bot);
            si.hwm       = (uint32_t)((uintptr_t)top    - (uintptr_t)bot);
            si.status    = STACK_CRITICAL;
        } else {
            // Canary resumes at p — incursion into BSS quantified; continue
            // scanning to find where canary gives way to live stack and
            // measure HWM.
            si.incursion = (uint32_t)((uintptr_t)p - (uintptr_t)bot);
            while (p < top && *p == CANARY_WORD) p++;
            si.hwm    = (uint32_t)((uintptr_t)top - (uintptr_t)p);
            si.status = STACK_OVERFLOW;
        }
    }
    return si;
}

void stack_render(stack_info_t si)
{
    switch (si.status) {
    case STACK_OK:
        print("OK  hwm="); printDec(si.hwm);
        print("/"); printDec(si.limit); print(" B");
        break;
    case STACK_WARN:
        print("WARNING  hwm="); printDec(si.hwm);
        print("/"); printDec(si.limit); print(" B  exceeds _Min_Stack_Size");
        break;
    case STACK_OVERFLOW:
        print("OVERFLOW  "); printDec(si.incursion); print("B into BSS");
        print("  hwm="); printDec(si.hwm);
        print("/"); printDec(si.limit); print(" B");
        pdump((Byte *)&_sstack, si.incursion < 40u ? si.incursion : 40u);
        break;
    case STACK_CRITICAL:
        print("CRITICAL  stack consumed all free RAM");
        print("  incursion>="); printDec(si.incursion); print("B");
        break;
    }
}
