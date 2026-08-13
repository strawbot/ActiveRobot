// Robot/io/mcp4728/mcp4728.h — MCP4728 quad 12-bit DAC with EEPROM.
//
// Board-agnostic: talks to the chip only through the mcp4728_hw_t function
// pointers the board supplies via mcp4728_bind_hw(). No vendor headers, no
// pin maps — see Robot/CLAUDE.md.
//
// Multi-instance: every call takes addr7, since several MCP4728s can share
// one bus (see Board/limb_io.c on Nano — one per limb, at 0x60-0x63 after
// one-time address assignment; see Board/mcp4728_addr_assign.h for that
// EEPROM-address-write step, which is hardware-timing-specific enough to
// live on the board side, not here).
//
// Register protocol per Microchip MCP4728 datasheet (DS22187E), Section
// 5.6 "General Commands" / Figures 5-7 through 5-15.

#ifndef ROBOT_IO_MCP4728_H
#define ROBOT_IO_MCP4728_H

#include <stdint.h>
#include <stdbool.h>

// Board-supplied raw I2C transaction hooks. Each call is a full
// START..STOP transaction (see Board/i2c1.h on Nano for the reference
// implementation). addr7 is the chip's 7-bit I2C address.
typedef struct {
    bool (*i2c_write)(uint8_t addr7, const uint8_t *data, uint32_t len);
    bool (*i2c_read)(uint8_t addr7, uint8_t *data, uint32_t len);
} mcp4728_hw_t;

// mcp4728_bind_hw — board supplies its I2C transaction functions. Call
// once, before any other mcp4728_* call.
void mcp4728_bind_hw(const mcp4728_hw_t *hw);

#define MCP4728_CHANNELS 4U

// mcp4728_set_channel — write a 12-bit code (0-4095) to DAC channel (0-3 =
// A-D), using the Multi-Write command (datasheet Sec. 5.6.2): DAC input
// register only, no EEPROM write (EEPROM has a finite write-cycle life and
// a ~50ms busy time per write — the Single/Sequential Write commands hit
// both and are deliberately not exposed here), VREF=Vdd, gain=1,
// UDAC=0 so VOUT updates immediately on this call regardless of the
// SEL/LDAC pin state. Returns false on I2C failure or an out-of-range
// channel.
bool mcp4728_set_channel(uint8_t addr7, uint8_t channel, uint16_t code12);

// mcp4728_read_channel — read back the DAC input register (the code last
// written by mcp4728_set_channel(), not the EEPROM copy) of channel (0-3),
// via the Read command (Sec. 5.6.9). Returns false on I2C failure or an
// out-of-range channel.
bool mcp4728_read_channel(uint8_t addr7, uint8_t channel, uint16_t *code12);

#endif // ROBOT_IO_MCP4728_H
