// Robot/cli/cli_transport.h — shared CLI-input glue for byte-stream
// transports (UART, USB CDC, ...).
//
// Every interactive transport repeats the same pattern: rebind EmitEvent to
// its own TX-drain function and turn autoEcho off before feeding a received
// byte into keyIn(), so the CLI always talks back through whichever
// transport last sent it a byte. This module factors out exactly that part
// — nothing hardware-specific.
//
// A board's transport shim keeps everything hardware-specific: its own RX
// queue/ISR, and an `emit` function that knows how to drain TimbreOS's
// global `emitq` through that transport's write primitive (see
// board-NanoR4/Board/cli_transport_usart.c for the reference shim). Unlike
// the single-instance feature_bind_hw() modules under Robot/io/, this one
// takes the hw struct per call rather than binding it once globally, since
// more than one transport (UART, USB CDC) can be live and swapping the
// active one at the same time.

#ifndef ROBOT_CLI_TRANSPORT_H
#define ROBOT_CLI_TRANSPORT_H

#include <stdint.h>

typedef struct {
    // Attempt to drain one unit of pending output (from the shared `emitq`)
    // through this transport. Must be non-blocking and safe to call from
    // both thread and ISR context; a no-op if there's nothing to send or the
    // transport isn't ready for more right now.
    void (*emit)(void);
} cli_transport_hw_t;

// Feed one received byte into the CLI, having this transport claim
// EmitEvent first. Call once per byte from the board's own RX action.
void cli_transport_rx_byte(const cli_transport_hw_t *hw, uint8_t byte);

#endif // ROBOT_CLI_TRANSPORT_H
