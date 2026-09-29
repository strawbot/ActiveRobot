// Robot/limbs/muscle/muscle_sr.h — muscle_hw_t shim over a 74HC595 chain.
//
// Realizes the muscle scheduler's on-windows on the shift register outputs.
// Muscle channel ch (0-23) drives output bit (ch % 8) of chip (ch / 8), the
// same order as the hex PCB (QA..QH of U_A/U_B/U_C = G_A1..G_C8).
//
// The PWM frame lasts MUSCLE_SR_FRAME_MS. Only window edges cost work: the
// runner sleeps (in()) until the next edge, then latches the new output
// pattern. A frame with no active muscle costs nothing. Edge timing rides
// the TEA scheduler, so an edge can be late by however long another action
// runs; keep actions short.
//
// Call after init_tea(), sr595_init() and muscle_init().

#ifndef ROBOT_MUSCLE_SR_H
#define ROBOT_MUSCLE_SR_H

#define MUSCLE_SR_FRAME_MS 20

void muscle_sr_init(void);   // bind to the muscle scheduler

#endif
