// Robot/io/ads1015/ads1015.h — ADS1015 4-channel single-ended 12-bit ADC.
//
// Board-agnostic: talks to the chip only through the ads1015_hw_t function
// pointers the board supplies via ads1015_bind_hw(). No vendor headers, no
// pin maps — see Robot/CLAUDE.md.
//
// Multi-instance: every call takes addr7, since several ADS1015s can share
// one bus (see Board/limb_io.c on Nano — one per limb, at 0x48-0x4B).
//
// Conversion is request/wait/read, not a single call: this chip has no
// on-chip channel sequencer (switching channels always needs a fresh
// conversion), and its ALERT/RDY "conversion ready" pin isn't wired on
// this board, so the caller must wait out the conversion time itself
// (ADS1015_CONV_TIME_MS below) instead of being interrupted. See
// Board/limb_io.c for the non-blocking start/after()/read sequencing.
//
// Register protocol per Texas Instruments ADS1015 datasheet (SBAS473),
// Device Register Map / Config Register sections.

#ifndef ROBOT_IO_ADS1015_H
#define ROBOT_IO_ADS1015_H

#include <stdint.h>
#include <stdbool.h>

// Board-supplied raw I2C transaction hooks. Each call is a full
// START..STOP transaction (see Board/i2c1.h on Nano for the reference
// implementation). addr7 is the chip's 7-bit I2C address.
typedef struct {
    bool (*i2c_write)(uint8_t addr7, const uint8_t *data, uint32_t len);
    bool (*i2c_read)(uint8_t addr7, uint8_t *data, uint32_t len);
} ads1015_hw_t;

// ads1015_bind_hw — board supplies its I2C transaction functions. Call
// once, before any other ads1015_* call.
void ads1015_bind_hw(const ads1015_hw_t *hw);

// Conversion time at DR=1600 SPS (~0.7 ms) rounded up generously — see
// file header. Caller waits this long (via after()) between
// ads1015_start_conversion() and ads1015_read_conversion().
#define ADS1015_CONV_TIME_MS 2U

#define ADS1015_CHANNELS 4U

// ads1015_start_conversion — begin a single-shot conversion of AINx (x =
// channel, 0-3) vs GND, at +-4.096V full scale. Returns false on I2C
// failure or an out-of-range channel.
bool ads1015_start_conversion(uint8_t addr7, uint8_t channel);

// ads1015_read_conversion — read back the result of the most recently
// started conversion. The chip is a 12-bit two's-complement ADC (left-
// justified in a 16-bit word, shifted down here), but single-ended AINx-
// vs-GND readings can never go negative, so *code12 only ever comes back
// 0-2047 (0V to +4.096V full scale, ~2mV/count) — not the full 0-4095 a
// plain unsigned 12-bit value might suggest. Call at least
// ADS1015_CONV_TIME_MS after ads1015_start_conversion(). Returns false on
// I2C failure.
bool ads1015_read_conversion(uint8_t addr7, uint16_t *code12);

#endif // ROBOT_IO_ADS1015_H
