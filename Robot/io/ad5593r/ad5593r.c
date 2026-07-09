// Robot/io/ad5593r/ad5593r.c — see ad5593r.h for the API contract and
// channel assignment.
//
// Register map, pointer byte layout, and data-word formats are taken
// directly from the AD5593R Rev. B datasheet (Analog Devices), Serial
// Interface / Configuring the AD5593R / DAC Write Operation / ADC
// Operation / Power-Down-Reference Control sections.

#include "ad5593r.h"
#include "timeout.h"
#include <stddef.h>

// Fixed 7-bit I2C address: six MSBs are 001000, LSB is the A0 pin state.
// Most AD5593R breakout boards tie A0 low.
#define AD5593R_I2C_ADDR 0x10U

// Pointer byte — mode nibble 0000 (configuration mode); D3:D0 select register.
#define AD5593R_REG_ADC_SEQUENCE   0x02U
#define AD5593R_REG_GEN_CTRL       0x03U
#define AD5593R_REG_ADC_CONFIG     0x04U
#define AD5593R_REG_DAC_CONFIG     0x05U
#define AD5593R_REG_PULLDOWN       0x06U
#define AD5593R_REG_LDAC_MODE      0x07U
#define AD5593R_REG_PD_REF_CTRL    0x0BU
#define AD5593R_REG_SW_RESET       0x0FU

// Mode nibbles (D7:D4 of the pointer byte).
#define AD5593R_MODE_DAC_WRITE     0x10U   // + dac channel (D2:D0)
#define AD5593R_MODE_ADC_READBACK  0x40U
#define AD5593R_MODE_REG_READBACK  0x70U   // + register address (D3:D0)

#define AD5593R_SW_RESET_KEY       0x0DACU

// Power-down/reference control register bits.
#define AD5593R_PDREF_EN_REF       (1U << 9)

#define AD5593R_DAC_CHANNELS 4U   // I/O0-I/O3
#define AD5593R_ADC_CHANNELS 4U   // I/O4-I/O7
#define AD5593R_ADC_IO_BASE  4U

static const ad5593r_hw_t *g_hw;

void ad5593r_bind_hw(const ad5593r_hw_t *hw) {
    g_hw = hw;
}

static bool ad5593r_write_reg(uint8_t pointer_byte, uint16_t data) {
    uint8_t buf[3] = { pointer_byte, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    return g_hw->i2c_write(AD5593R_I2C_ADDR, buf, sizeof(buf));
}

static bool ad5593r_read_word(uint8_t pointer_byte, uint16_t *out) {
    if (!g_hw->i2c_write(AD5593R_I2C_ADDR, &pointer_byte, 1)) {
        return false;
    }
    uint8_t buf[2];
    if (!g_hw->i2c_read(AD5593R_I2C_ADDR, buf, sizeof(buf))) {
        return false;
    }
    *out = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

bool ad5593r_init(void) {
    if (g_hw == NULL) {
        return false;
    }

    bool ok = ad5593r_write_reg(AD5593R_REG_SW_RESET, AD5593R_SW_RESET_KEY);
    ms_blocking(1);   // reset takes 100 us max; no writes allowed during that time

    ok = ad5593r_write_reg(AD5593R_REG_DAC_CONFIG, 0x000FU) && ok;   // I/O0-I/O3 = DAC
    ok = ad5593r_write_reg(AD5593R_REG_ADC_CONFIG, 0x00F0U) && ok;   // I/O4-I/O7 = ADC
    ok = ad5593r_write_reg(AD5593R_REG_PULLDOWN, 0x0000U) && ok;     // clear 85k pull-downs
    ok = ad5593r_write_reg(AD5593R_REG_GEN_CTRL, 0x0000U) && ok;     // 0-VREF range, no gain
    ok = ad5593r_write_reg(AD5593R_REG_LDAC_MODE, 0x0000U) && ok;    // immediate DAC update
    ok = ad5593r_write_reg(AD5593R_REG_PD_REF_CTRL, AD5593R_PDREF_EN_REF) && ok;

    return ok;
}

bool ad5593r_set_dac(uint8_t channel, uint16_t code12) {
    if (g_hw == NULL || channel >= AD5593R_DAC_CHANNELS) {
        return false;
    }
    return ad5593r_write_reg(AD5593R_MODE_DAC_WRITE | channel, code12 & 0x0FFFU);
}

bool ad5593r_read_adc(uint8_t channel, uint16_t *code12) {
    if (g_hw == NULL || channel >= AD5593R_ADC_CHANNELS || code12 == NULL) {
        return false;
    }

    uint8_t io_pin = (uint8_t)(AD5593R_ADC_IO_BASE + channel);

    // Select exactly this one channel for a single-shot (non-repeating)
    // conversion sequence, then read the one result it produces.
    if (!ad5593r_write_reg(AD5593R_REG_ADC_SEQUENCE, (uint16_t)(1U << io_pin))) {
        return false;
    }

    uint16_t raw;
    if (!ad5593r_read_word(AD5593R_MODE_ADC_READBACK, &raw)) {
        return false;
    }

    *code12 = raw & 0x0FFFU;
    return true;
}

bool ad5593r_read_status(ad5593r_status_t *status) {
    if (g_hw == NULL || status == NULL) {
        return false;
    }

    uint16_t adc_cfg, dac_cfg, pd_ref;
    bool ok = ad5593r_read_word(AD5593R_MODE_REG_READBACK | AD5593R_REG_ADC_CONFIG, &adc_cfg);
    ok = ad5593r_read_word(AD5593R_MODE_REG_READBACK | AD5593R_REG_DAC_CONFIG, &dac_cfg) && ok;
    ok = ad5593r_read_word(AD5593R_MODE_REG_READBACK | AD5593R_REG_PD_REF_CTRL, &pd_ref) && ok;
    if (!ok) {
        return false;
    }

    status->adc_config  = (uint8_t)(adc_cfg & 0xFFU);
    status->dac_config  = (uint8_t)(dac_cfg & 0xFFU);
    status->ref_enabled = (pd_ref & AD5593R_PDREF_EN_REF) != 0;
    return true;
}
