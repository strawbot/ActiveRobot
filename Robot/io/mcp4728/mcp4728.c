// Robot/io/mcp4728/mcp4728.c — see mcp4728.h for the API contract.
//
// Multi-Write command (datasheet Fig. 5-8), 3 bytes after device
// addressing, one channel per call:
//   byte2 = C2 C1 C0 W1 W0 DAC1 DAC0 UDAC = 0 1 0 0 0 <ch:2> 0
//         = 0x40 | (channel << 1)   (UDAC always 0 — see mcp4728_set_channel)
//   byte3 = VREF PD1 PD0 Gx D11 D10 D9 D8 = 0 0 0 0 <D11:D8>
//         = (code12 >> 8) & 0x0F   (VREF=0 -> Vdd reference, PD=00 -> normal
//           operation, Gx=0 -> gain irrelevant when VREF=0)
//   byte4 = D7 D6 D5 D4 D3 D2 D1 D0 = code12 & 0xFF
//
// Read command (datasheet Fig. 5-15): the chip streams channel A's DAC
// input register (3 bytes), then channel A's EEPROM (3 bytes), then B's
// input register, B's EEPROM, and so on through D — 6 bytes per channel,
// no way to seek directly to one channel's input register, so reading
// channel N means reading and discarding everything before it.

#include "mcp4728.h"
#include <stddef.h>

#define MCP4728_MULTIWRITE_CMD 0x40U   // C2 C1 C0 W1 W0 = 0 1 0 0 0

#define MCP4728_BYTES_PER_CHANNEL_BLOCK 6U   // 3 (input reg) + 3 (eeprom)
#define MCP4728_BYTES_INPUT_REG         3U

static const mcp4728_hw_t *g_hw;

void mcp4728_bind_hw(const mcp4728_hw_t *hw) {
    g_hw = hw;
}

bool mcp4728_set_channel(uint8_t addr7, uint8_t channel, uint16_t code12) {
    if (g_hw == NULL || channel >= MCP4728_CHANNELS) {
        return false;
    }

    code12 &= 0x0FFFU;
    uint8_t buf[3] = {
        (uint8_t)(MCP4728_MULTIWRITE_CMD | (channel << 1)),   // UDAC = 0
        (uint8_t)(code12 >> 8),                                // VREF=0,PD=00,Gx=0
        (uint8_t)(code12 & 0xFFU),
    };
    return g_hw->i2c_write(addr7, buf, sizeof(buf));
}

bool mcp4728_read_channel(uint8_t addr7, uint8_t channel, uint16_t *code12) {
    if (g_hw == NULL || channel >= MCP4728_CHANNELS || code12 == NULL) {
        return false;
    }

    uint32_t len = (uint32_t)channel * MCP4728_BYTES_PER_CHANNEL_BLOCK + MCP4728_BYTES_INPUT_REG;
    uint8_t buf[MCP4728_CHANNELS * MCP4728_BYTES_PER_CHANNEL_BLOCK];
    if (!g_hw->i2c_read(addr7, buf, len)) {
        return false;
    }

    // Last 3 bytes read are this channel's DAC input register.
    uint8_t *ch_reg = &buf[len - MCP4728_BYTES_INPUT_REG];
    *code12 = (((uint16_t)ch_reg[1] & 0x0FU) << 8) | ch_reg[2];
    return true;
}
