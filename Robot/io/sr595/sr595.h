// Robot/io/sr595/sr595.h — chain of 74HC595 shift registers.
//
// Board-agnostic: the board supplies three pin-level hooks (SER, SRCLK,
// RCLK) through sr595_bind_hw(); no vendor headers, no pin maps — see
// Robot/CLAUDE.md. /OE and /SRCLR are wired by the board, not driven here.
//
// The driver keeps a shadow byte per chip. sr595_put()/sr595_set_bits()
// edit the shadow only; sr595_latch() shifts the whole chain out and pulses
// RCLK so every output changes together.
//
// Chain order: chip 0 is the first chip after SER (nearest the MCU), chip 1
// follows it, and so on. Bit n of a chip's byte drives output Q(A+n): bit 0
// = QA (pin 15), bit 1 = QB (pin 1), ... bit 7 = QH (pin 7).

#ifndef ROBOT_IO_SR595_H
#define ROBOT_IO_SR595_H

#include <stdint.h>
#include <stdbool.h>

#define SR595_MAX_CHIPS 3U   // hex board: 3 chained, 24 outputs

// Board-supplied pin hooks. Each sets the pin high (true) or low (false).
typedef struct {
    void (*ser)(bool level);
    void (*srclk)(bool level);
    void (*rclk)(bool level);
    // Optional fast paths, used from an interrupt (NULL = build them from the
    // pin hooks above, which is slower). shift() clocks the whole chain in,
    // last chip first, with no latch; bytes[0] is chip 0. strobe() is one
    // RCLK pulse.
    void (*shift)(const uint8_t *bytes, uint8_t chips);
    void (*strobe)(void);
} sr595_hw_t;

// sr595_bind_hw — call once, before sr595_init().
void sr595_bind_hw(const sr595_hw_t *hw);

// sr595_init — set the number of chips in the chain (1..SR595_MAX_CHIPS),
// clear the shadow, and latch all-zero outputs. Returns false if no hw is
// bound or chips is out of range.
bool sr595_init(uint8_t chips);

// sr595_put — set chip's whole shadow byte. Does not touch the outputs.
bool sr595_put(uint8_t chip, uint8_t value);

// sr595_set_bits — within chip's shadow, set the bits in mask to on
// (true) or off (false). Does not touch the outputs.
bool sr595_set_bits(uint8_t chip, uint8_t mask, bool on);

// sr595_get — chip's shadow byte (0 if out of range).
uint8_t sr595_get(uint8_t chip);

// sr595_all_off — shift zeros through the whole chain and latch them, using
// only the bound pin hooks: no scheduler, no shadow bookkeeping, callable from
// a fault handler or system_failure(). Does nothing if no hardware is bound.
void sr595_all_off(void);

// ISR-safe pieces, for a timer interrupt that pre-shifts the next pattern and
// then latches it at an exact time. mask bit (8 * chip + bit) drives output
// `bit` of `chip`. None of these take locks or call the scheduler.
void sr595_shift_mask(uint32_t mask);    // clock mask into the chain; outputs do not change
void sr595_strobe(void);                 // RCLK pulse: the shifted pattern appears on the outputs
void sr595_note_latched(uint32_t mask);  // keep the shadow bytes in step after a strobe

// sr595_take_dirty — true once after sr595_latch() has run (it reshuffles the
// shift register), so a caller that pre-shifts knows to shift again.
bool sr595_take_dirty(void);

// sr595_latch — shift the shadow out and pulse RCLK, with interrupts masked so
// a timer ISR cannot interleave its own shifting. Blocking, but short.
void sr595_latch(void);

// CLI words
void sr595_cli_write(void);   // ( value chip -- ) put + latch
void sr595_cli_read(void);    // ( chip -- value ) shadow byte

#endif // ROBOT_IO_SR595_H
