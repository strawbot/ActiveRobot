// Robot/io/sr595/sr595.c — see sr595.h for the API contract.
//
// 74HC595 protocol: SER is sampled on the rising edge of SRCLK, MSB-first
// order is not required by the chip but is used here so the byte for the
// LAST chip in the chain goes out first and each chip's bit 7 (QH) is the
// first bit it receives. RCLK rising copies the shift register to the
// outputs.

#include "sr595.h"
#include "cli.h"
#include "printers.h"
#include "tea.h"
#include <stddef.h>

static const sr595_hw_t *hw;
static uint8_t chips;
static uint8_t shadow[SR595_MAX_CHIPS];
static volatile bool dirty;                  // set by sr595_latch()

void sr595_bind_hw(const sr595_hw_t *ops) {
    hw = ops;
}

bool sr595_init(uint8_t n) {
    if (!hw || n == 0 || n > SR595_MAX_CHIPS) return false;
    chips = n;
    for (uint8_t i = 0; i < SR595_MAX_CHIPS; i++) shadow[i] = 0;
    hw->ser(false);
    hw->srclk(false);
    hw->rclk(false);
    sr595_latch();
    return true;
}

bool sr595_put(uint8_t chip, uint8_t value) {
    if (chip >= chips) return false;
    shadow[chip] = value;
    return true;
}

bool sr595_set_bits(uint8_t chip, uint8_t mask, bool on) {
    if (chip >= chips) return false;
    if (on) shadow[chip] |= mask;
    else    shadow[chip] &= (uint8_t)~mask;
    return true;
}

uint8_t sr595_get(uint8_t chip) {
    return chip < chips ? shadow[chip] : 0;
}

void sr595_all_off(void) {
    if (!hw) return;
    for (uint8_t i = 0; i < SR595_MAX_CHIPS * 8U; i++) {
        hw->ser(false);
        hw->srclk(true);
        hw->srclk(false);
    }
    hw->rclk(true);
    hw->rclk(false);
    for (uint8_t c = 0; c < SR595_MAX_CHIPS; c++) shadow[c] = 0;
}

void sr595_latch(void) {
    if (!hw || chips == 0) return;
    safe(
        for (int c = (int)chips - 1; c >= 0; c--) {
            for (int bit = 7; bit >= 0; bit--) {
                hw->ser((shadow[c] >> bit) & 1U);
                hw->srclk(true);
                hw->srclk(false);
            }
        }
        hw->rclk(true);
        hw->rclk(false);
        dirty = true;
    )
}

bool sr595_take_dirty(void) {
    bool d = dirty;
    dirty = false;
    return d;
}

void sr595_shift_mask(uint32_t mask) {
    if (!hw) return;
    uint8_t b[SR595_MAX_CHIPS];
    for (uint8_t c = 0; c < chips; c++) b[c] = (uint8_t)(mask >> (8U * c));
    if (hw->shift) { hw->shift(b, chips); return; }
    for (int c = (int)chips - 1; c >= 0; c--) {
        for (int bit = 7; bit >= 0; bit--) {
            hw->ser((b[c] >> bit) & 1U);
            hw->srclk(true);
            hw->srclk(false);
        }
    }
}

void sr595_strobe(void) {
    if (!hw) return;
    if (hw->strobe) { hw->strobe(); return; }
    hw->rclk(true);
    hw->rclk(false);
}

void sr595_note_latched(uint32_t mask) {
    for (uint8_t c = 0; c < chips; c++) shadow[c] = (uint8_t)(mask >> (8U * c));
}

// Clamp a CLI cell into 0..255 so a stray value can't wrap onto a chip.
static uint8_t arg(Integer v) {
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

void sr595_cli_write(void) {   // ( value chip -- )
    uint8_t chip  = arg((Integer)ret());
    uint8_t value = arg((Integer)ret());
    if (!sr595_put(chip, value)) { print("\nsr595: no such chip"); return; }
    sr595_latch();
}

void sr595_cli_read(void) {    // ( chip -- value )
    uint8_t chip = arg((Integer)ret());
    lit((Cell)sr595_get(chip));
}
