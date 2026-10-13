// Robot/limbs/wirecal/wirecal.h — per-wire self-calibration.
//
// Ramps one muscle's activation upward, and at each step measures the wire's
// resistance R = (VIN - I*R_series) / I from the current through its sense
// resistor and the supply voltage, sampled during the wire's own on-window.
// From the resulting resistance-versus-activation curve it finds the wire's
// phase transition (see wirecal_analyze.h): the activation where the wire
// starts to move, where it moves fastest, and where it has finished. Those
// are properties of the wire, so no resistor or ADC needs to be exact.
//
// While it runs, only the wire's own limb is fitted (muscle_set_fitted), so the
// muscle scheduler gives that wire a slot of MUSCLE_FRAME / 2 instead of the
// 1/8 it gets in normal 4-limb operation. The window is then long enough to
// sample reliably, and the ramp reaches a higher average power. Results are
// therefore stored as DUTY, in tenths of a percent of the frame, not as an
// activation percentage: that stays meaningful under any scheduling, and it
// tells a later layer whether the wire's transition is reachable at all in
// normal operation (its duty there is at most 1000 / (2 * fitted limbs)).
//
// Run `halt` first so a gait is not also driving muscles.
//
// Results are kept per muscle, saved through Robot/storage/calib, and read
// back with wirecal_get(). A later layer can map an activation of 0-100 % onto
// [onset, full] instead of a fixed range.
//
// Board-agnostic: the board supplies the measurement through wirecal_hw_t.
// Needs muscle_sr (window timing) and the TEA scheduler; call wirecal_init()
// after muscle_sr_init() and after the scheduler is running.

#ifndef ROBOT_WIRECAL_H
#define ROBOT_WIRECAL_H

#include "wirecal_analyze.h"
#include <stdint.h>
#include <stdbool.h>

// Safety and timing. The limits are deliberately conservative: they only need
// to stop a short, a wrong wire or a runaway ramp, not to be accurate.
#define WC_START_PCT     15U      // first activation (window long enough to sample)
#define WC_STEP_PCT       3U
#define WC_MAX_PCT      100U
#define WC_REST_MS     2000U      // wire cools before the ramp starts
#define WC_DWELL_MS    1000U      // thermal settle at each step
#define WC_SAMPLES        8U      // on-window samples taken per step (median used)
#define WC_SAMPLE_US    250U      // delay after a window opens before sampling
#define WC_MEASURE_MS  2000U      // give up on a step that yields too few samples
#define WC_MIN_ON_MA     20U      // below this while on: no wire, or an open circuit
#define WC_MAX_ON_MA   1000U      // peak while a window is open (a 350 mA-rated wire pulses ~700 mA at 21 V);
                                  // above this: a short (21 V into ~1 ohm is ~20 A) or the wrong wire
#define WC_MAX_AVG_MA   250U      // stop the ramp when peak x duty reaches this: under the wire's 350 mA rating
#define WC_MAX_POINTS    40U

// Duty (tenths of a percent of the frame) per percent of activation while one
// limb is fitted: the slot is 1/2 of the frame, so 100 % activation = 500.
#define WC_DUTY_PER_PCT   5U

// Board-supplied measurement.
typedef struct {
    // Current through channel ch's sense resistor (mA) and the supply voltage
    // (mV), read at the same instant. false when ch has no sense tap or no
    // fresh reading. Called while ch's window is open.
    bool (*sample)(uint8_t ch, uint32_t *ma, uint32_t *vin_mv);
    // Resistance between the wire and ground that is not the wire, milliohm:
    // sense resistor + MOSFET on-resistance + wiring.
    uint32_t series_mohm;
    // Optional, for diagnostics: the mV on each analog tap right now, in the
    // order A0, A1, A2, A3 (NULL if the board has no such view).
    void (*taps)(uint32_t mv[4]);
} wirecal_hw_t;

void wirecal_bind_hw(const wirecal_hw_t *hw);
void wirecal_init(void);                 // hook the edge callback and the calib file

bool wirecal_start(uint8_t muscle);      // muscle 1-24; false if busy or not measurable
void wirecal_stop(void);                 // abort and relax
bool wirecal_running(void);
bool wirecal_get(uint8_t muscle, wire_cal_t *out);   // false if not calibrated

// CLI words
void wirecal_cli(void);          // ( muscle -- )  wirecal
void wirecal_cli_stop(void);     // ( -- )         wirecal-stop
void wirecal_cli_show(void);     // ( -- )         wires
void wirecal_cli_set(void);      // ( muscle rcold onset mid full -- )  wire!
void wirecal_cli_scope(void);    // ( muscle -- )  scope: ADC readings across one pulse of that wire
void wirecal_cli_isns(void);     // ( -- )         isns: what the sense taps read while the wires pulse

#endif
