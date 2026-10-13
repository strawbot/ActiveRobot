// Robot/limbs/muscle/muscle_sr.h — muscle_hw_t shim over a 74HC595 chain,
// timed by a hardware timer.
//
// Realizes the muscle scheduler's on-windows on the shift register outputs.
// Muscle channel ch (0-23) drives output bit (ch % 8) of chip (ch / 8), the
// same order as the hex PCB (QA..QH of U_A/U_B/U_C = G_A1..G_C8).
//
// Timing is a free-running board timer that wraps once per frame
// (MUSCLE_SR_FRAME_MS). The window edges of a frame are a sorted list of
// (time, pattern). At each edge the timer interrupt does only the time-critical
// part: it pulses RCLK to latch the pattern that was shifted in ahead of time,
// shifts in the pattern for the following edge, and arms the next compare
// match. Nothing here uses the scheduler's in()/after() list, which is too
// heavy for edges this frequent and too jittery for their timing.
//
// A schedule change (commit) is built in task context into a spare buffer and
// adopted at the next frame start, so the interrupt never sees a half-built one.
// The change reaches the wires within one frame (20 ms).
//
// Fail-safe: muscle_sr_emergency_stop() stops the timer and clears the chain,
// for system_failure() and the fault handlers. Without it a hung scheduler
// would not stop the edges, because the timer keeps running.

#ifndef ROBOT_MUSCLE_SR_H
#define ROBOT_MUSCLE_SR_H

#include <stdint.h>
#include <stdbool.h>

#define MUSCLE_SR_FRAME_MS 20

// Board-supplied frame timer. A free-running up-counter that wraps every
// frame_counts. The board's interrupt handlers call muscle_sr_isr_frame()
// when it wraps, muscle_sr_isr_edge() on a compare-A match and
// muscle_sr_isr_sample() on a compare-B match; the handlers clear their own
// interrupt flags. Edge interrupts must outrank everything else the board runs;
// the sample interrupt should rank below them.
typedef struct {
    uint32_t hz;                          // timer counts per second
    uint32_t frame_counts;                // counts per frame; MUSCLE_SR_FRAME_MS worth
    void     (*start)(void);              // start counting from 0
    void     (*stop)(void);               // stop counting and mask the three interrupts
    uint32_t (*count)(void);              // current count, 0 .. frame_counts - 1
    void     (*arm_edge)(uint32_t counts);    // compare A: interrupt when count == counts
    void     (*arm_sample)(uint32_t counts);  // compare B: interrupt when count == counts
} muscle_sr_timer_t;

void muscle_sr_bind_timer(const muscle_sr_timer_t *t);   // call before muscle_sr_init()
// muscle_sr_init — bind to the muscle scheduler and start the timer. First it
// proves the frame and compare interrupts fire (about 40 ms, blocking): a timer
// whose interrupts are not wired up would latch the first window on and never
// turn it off. If the test fails nothing is ever latched; see muscle_sr_fault().
void muscle_sr_init(void);

// Interrupt entry points, called by the board's timer handlers. Keep them out
// of the scheduler: no print, no in(), no allocation.
void muscle_sr_isr_frame(void);
void muscle_sr_isr_edge(void);
void muscle_sr_isr_sample(void);

// Why the timer was stopped, if it was. Anything other than MUSCLE_SR_OK means no
// pattern is being latched and the chain is cleared.
typedef enum {
    MUSCLE_SR_OK = 0,
    MUSCLE_SR_FAULT_NO_FRAME_IRQ,   // start-up self-test: the frame interrupt never fired
    MUSCLE_SR_FAULT_NO_EDGE_IRQ,    // start-up self-test: the compare interrupt never fired
    MUSCLE_SR_FAULT_EDGES_MISSED,   // two frames running with edges left unlatched
    MUSCLE_SR_FAULT_STOPPED         // muscle_sr_emergency_stop()
} muscle_sr_fault_t;
muscle_sr_fault_t muscle_sr_fault(void);

// Stop the timer and clear the whole chain. Callable from a fault handler or
// system_failure(), in any context.
void muscle_sr_emergency_stop(void);

// muscle_sr_is_on — true while channel ch (0-23) is latched on.
bool muscle_sr_is_on(uint8_t ch);

// muscle_sr_mask — the pattern latched now (bit ch = channel ch).
uint32_t muscle_sr_mask(void);

// muscle_sr_window_us — length of channel ch's window in the active schedule,
// microseconds (0 = not scheduled).
uint32_t muscle_sr_window_us(uint8_t ch);

// muscle_sr_edge_age_us — microseconds since the last edge was latched.
uint32_t muscle_sr_edge_age_us(void);

// muscle_sr_on_sample — cb is called from the sample interrupt delay_us after
// each edge, with the pattern latched then (an edge that has been superseded by
// a newer one is skipped). cb runs in interrupt context: read a value, push it
// somewhere, and hand the rest to later(). NULL clears it. delay_us should
// exceed a few microseconds so the compare is still in the future.
void muscle_sr_on_sample(void (*cb)(uint32_t mask), uint32_t delay_us);

#endif
