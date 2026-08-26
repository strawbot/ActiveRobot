#include "tea.h"
#include "cli.h"

#include "cli_transport.h"

static const cli_transport_hw_t *active_hw;

static void cli_transport_emit(void) {
    if (active_hw) active_hw->emit();
}

void cli_transport_rx_byte(const cli_transport_hw_t *hw, uint8_t byte) {
    active_hw = hw;
    when(EmitEvent, cli_transport_emit);
    autoEchoOff();
    keyIn((Byte) byte);
}
