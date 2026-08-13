// Robot/io/ad5593r/ad5593r.h — AD5593R 8-channel configurable ADC/DAC driver.
//
// Board-agnostic: talks to the chip only through the ad5593r_hw_t function
// pointers the board supplies via ad5593r_bind_hw(). No vendor headers, no
// pin maps — see Robot/CLAUDE.md.
//
// Fixed channel assignment (matches ad5593r_init()):
//   I/O0-I/O3  ->  DAC outputs, addressed here as dac channel 0-3
//   I/O4-I/O7  ->  ADC inputs,  addressed here as adc channel 0-3
//
// Register protocol per Analog Devices AD5593R Rev. B datasheet
// (Serial Interface / Pointer Byte / Control Registers / DAC Write /
// ADC Operation / Power-Down/Reference Control sections).

#ifndef ROBOT_IO_AD5593R_H
#define ROBOT_IO_AD5593R_H

#include <stdint.h>
#include <stdbool.h>

// Board-supplied raw I2C transaction hooks. Each call is a full
// START..STOP transaction (see Board/i2c0.h on Nano for the reference
// implementation). addr7 is the chip's 7-bit I2C address.
typedef struct {
    bool (*i2c_write)(uint8_t addr7, const uint8_t *data, uint32_t len);
    bool (*i2c_read)(uint8_t addr7, uint8_t *data, uint32_t len);
} ad5593r_hw_t;

// ad5593r_bind_hw — board supplies its I2C transaction functions. Call
// once, before ad5593r_init().
void ad5593r_bind_hw(const ad5593r_hw_t *hw);

// ad5593r_init — software-reset the chip, wait for reset to complete,
// then configure I/O0-I/O3 as DAC outputs, I/O4-I/O7 as ADC inputs, and
// enable the internal 2.5V reference (0-VREF range on both ADC and DAC).
// Returns false if any transaction was NACKed (chip not present/wired).
bool ad5593r_init(void);

// ad5593r_reset — same as ad5593r_init(). Exposed separately so a CLI word
// can restore known-good state (e.g. after a wedged bus or a DAC channel
// left mid-ramp) without implying "first-time bring-up".
bool ad5593r_reset(void);

// ad5593r_set_dac — write a 12-bit code (0-4095) to DAC channel (0-3),
// i.e. I/O(channel). Output is code/4095 * 2.5V (internal reference,
// 0-VREF range). Returns false on I2C failure or an out-of-range channel.
bool ad5593r_set_dac(uint8_t channel, uint16_t code12);

// ad5593r_read_adc — convert ADC channel (0-3), i.e. I/O(4+channel), and
// return the 12-bit result (0-4095) in *code12. Returns false on I2C
// failure or an out-of-range channel.
bool ad5593r_read_adc(uint8_t channel, uint16_t *code12);

// ad5593r_read_dac — read back the input register of DAC channel (0-3),
// i.e. I/O(channel) — the code last written by ad5593r_set_dac(), not a
// fresh conversion. Returns false on I2C failure or an out-of-range
// channel.
bool ad5593r_read_dac(uint8_t channel, uint16_t *code12);

// ad5593r_read_temp — read the on-chip temperature indicator (die
// temperature, not tied to any I/O pin) and return degrees Celsius in
// *temp_c. Datasheet accuracy is ~3 degrees C; only valid at ADC gain=1
// (0-VREF range, the default ad5593r_init() sets). Returns false on I2C
// failure.
bool ad5593r_read_temp(float *temp_c);

// ad5593r_status_t — readback of the registers that matter for a
// diagnostic dump.
typedef struct {
    uint8_t adc_config;   // bit n set -> I/O(n) is an ADC input
    uint8_t dac_config;   // bit n set -> I/O(n) is a DAC output
    bool    ref_enabled;  // internal 2.5V reference powered up (EN_REF)
} ad5593r_status_t;

// ad5593r_read_status — read back ADC_CONFIG, DAC_CONFIG, and the
// power-down/reference control register. Returns false on I2C failure.
bool ad5593r_read_status(ad5593r_status_t *status);

#endif // ROBOT_IO_AD5593R_H
