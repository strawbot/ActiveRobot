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

// sr595_latch — shift the shadow out and pulse RCLK. Blocking, but short
// (8 clocks per chip, no delays).
void sr595_latch(void);

// CLI words
void sr595_cli_write(void);   // ( value chip -- ) put + latch
void sr595_cli_read(void);    // ( chip -- value ) shadow byte

#endif // ROBOT_IO_SR595_H
