// Robot/io/ads1015/ads1015.c — see ads1015.h for the API contract.
//
// Config register layout (16 bits), Config Register section of the
// datasheet:
//   OS      bit 15   1 = start a single conversion (write); busy flag (read)
//   MUX     bits 14:12  100+ch = AINch vs GND, single-ended
//   PGA     bits 11:9   001 = +-4.096V full scale
//   MODE    bit 8    1 = single-shot (power down between conversions)
//   DR      bits 7:5    100 = 1600 SPS
//   COMP_*  bits 4:0    comparator disabled (ALERT/RDY pin unused/unwired
//                       on this board) — POR default is already "disabled"
//                       (COMP_QUE=11) but set explicitly since MODE/DR
//                       above are in the same write.

#include "ads1015.h"
#include <stddef.h>

#define ADS1015_REG_CONVERSION 0x00U
#define ADS1015_REG_CONFIG     0x01U

#define ADS1015_CFG_OS_START     (1U << 15)
#define ADS1015_CFG_MUX_SINGLE(ch) ((uint16_t)(0x4U | (ch)) << 12)
#define ADS1015_CFG_PGA_4V096    (0x1U << 9)
#define ADS1015_CFG_MODE_SINGLE  (1U << 8)
#define ADS1015_CFG_DR_1600SPS   (0x4U << 5)
#define ADS1015_CFG_COMP_DISABLE (0x3U)   // COMP_QUE = 11

static const ads1015_hw_t *g_hw;

void ads1015_bind_hw(const ads1015_hw_t *hw) {
    g_hw = hw;
}

bool ads1015_start_conversion(uint8_t addr7, uint8_t channel) {
    if (g_hw == NULL || channel >= ADS1015_CHANNELS) {
        return false;
    }

    uint16_t cfg = ADS1015_CFG_OS_START
                 | ADS1015_CFG_MUX_SINGLE(channel)
                 | ADS1015_CFG_PGA_4V096
                 | ADS1015_CFG_MODE_SINGLE
                 | ADS1015_CFG_DR_1600SPS
                 | ADS1015_CFG_COMP_DISABLE;

    uint8_t buf[3] = { ADS1015_REG_CONFIG, (uint8_t)(cfg >> 8), (uint8_t)(cfg & 0xFFU) };
    return g_hw->i2c_write(addr7, buf, sizeof(buf));
}

bool ads1015_read_config(uint8_t addr7, uint16_t *config) {
    if (g_hw == NULL || config == NULL) {
        return false;
    }

    uint8_t ptr = ADS1015_REG_CONFIG;
    if (!g_hw->i2c_write(addr7, &ptr, 1)) {
        return false;
    }

    uint8_t buf[2];
    if (!g_hw->i2c_read(addr7, buf, sizeof(buf))) {
        return false;
    }

    *config = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

bool ads1015_read_conversion(uint8_t addr7, uint16_t *code12) {
    if (g_hw == NULL || code12 == NULL) {
        return false;
    }

    uint8_t ptr = ADS1015_REG_CONVERSION;
    if (!g_hw->i2c_write(addr7, &ptr, 1)) {
        return false;
    }

    uint8_t buf[2];
    if (!g_hw->i2c_read(addr7, buf, sizeof(buf))) {
        return false;
    }

    // 12-bit result is left-justified in the 16-bit word.
    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    *code12 = (raw >> 4) & 0x0FFFU;
    return true;
}
