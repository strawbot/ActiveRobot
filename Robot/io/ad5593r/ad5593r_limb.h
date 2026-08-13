// Robot/io/ad5593r/ad5593r_limb.h — muscle-wire limb calibration/control
// built on top of the AD5593R's DAC/ADC channel pairs (see ad5593r.h).
//
// Each of the AD5593R's 4 DAC/ADC channel pairs drives one muscle-wire
// "limb": DAC channel n sets the drive level (0-2500 mV, 12-bit code
// 0-4095), ADC channel n reads back a sense voltage. The DAC->ADC
// transfer curve has a dead zone at low drive, a linear region, then
// saturates near full scale. ad5593r_limb_calibrate() ramps a limb's DAC
// up one code at a time from 0 until the ADC saturates, finds the linear
// region of that response, and records its DAC bounds. ad5593r_limb_set()
// then drives a limb by percentage (0-100) within its calibrated window.
//
// Board-agnostic: depends only on ad5593r.h and TimbreOS (ms_blocking).

#ifndef ROBOT_IO_AD5593R_LIMB_H
#define ROBOT_IO_AD5593R_LIMB_H

#include <stdint.h>
#include <stdbool.h>

#define AD5593R_LIMB_COUNT 4U

typedef struct {
    bool     calibrated;
    uint16_t dac_lo_mv;   // DAC mV at the start of the linear region (0%)
    uint16_t dac_hi_mv;   // DAC mV at the end of the linear region (100%)
} ad5593r_limb_cal_t;

// ad5593r_limb_calibrate — ramp limb's (0-3) DAC 0 -> 4095 one code at a
// time until the ADC reads at/near full scale, find the linear region of
// that response, and store the result. Leaves the DAC at 0 afterward
// regardless of outcome. This blocks for the whole ramp (worst case ~41s
// if the limb never saturates) — see Robot/io/ad5593r/ad5593r_limb.c for
// the tuning knobs and the cooperative-multitasking tradeoff this makes.
// Returns false on an out-of-range limb, I2C failure, the post-sweep
// safety reset failing, or no linear region being found (calibration
// state is left unchanged in that case).
bool ad5593r_limb_calibrate(uint8_t limb);

// ad5593r_limb_calibrate_all — calibrate limbs 0-3 in turn.
// Returns false if any limb failed to calibrate.
bool ad5593r_limb_calibrate_all(void);

// ad5593r_limb_set — drive limb (0-3) to pct percent (0-100, clamped) of
// its calibrated linear range. Returns false if the limb isn't
// calibrated yet, on an out-of-range limb, or on I2C failure.
bool ad5593r_limb_set(uint8_t limb, uint8_t pct);

// ad5593r_limb_get_cal — fetch the calibration for limb (0-3), for
// diagnostics. Returns false for an out-of-range limb or NULL cal.
bool ad5593r_limb_get_cal(uint8_t limb, ad5593r_limb_cal_t *cal);

#endif // ROBOT_IO_AD5593R_LIMB_H
