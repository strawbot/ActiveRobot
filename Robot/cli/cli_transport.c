#include "tea.h"
#include "cli.h"

#include "cli_transport.h"

static const cli_transport_hw_t *active_hw;

static void cli_transport_emit(void) {
    if (active_hw) active_hw->emit();
}

void cli_transport_claim(const cli_transport_hw_t *hw) {
    active_hw = hw;
    when(EmitEvent, cli_transport_emit);
}

void cli_transport_rx_byte(const cli_transport_hw_t *hw, uint8_t byte) {
    cli_transport_claim(hw);
    autoEchoOff();
    keyIn((Byte) byte);
}

const char *cli_transport_active_name(void) {
    return active_hw ? active_hw->name : "none";
}
