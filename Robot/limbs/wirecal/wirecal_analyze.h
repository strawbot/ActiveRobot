// Robot/limbs/wirecal/wirecal_analyze.h — find a muscle wire's phase
// transition in its resistance-versus-activation curve.
//
// Pure computation, no hardware or OS calls, so it can be tested on a host.
// The curve is R (milliohm) measured at increasing drive x, in any unit the
// caller chooses (wirecal uses duty in tenths of a percent of the frame).
// Nitinol's resistance changes as it contracts, and the direction can differ
// (R-phase rise then fall), so the transition is found by SLOPE MAGNITUDE, not
// by a fixed sign or an absolute resistance. That is what makes the result a
// property of the wire itself and independent of resistor tolerances.

#ifndef ROBOT_WIRECAL_ANALYZE_H
#define ROBOT_WIRECAL_ANALYZE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool     valid;
    uint32_t rcold;         // baseline resistance, milliohm (mean of the first points)
    uint16_t onset;         // x where the resistance starts to move
    uint16_t mid;           // x of the steepest change (the inflection)
    uint16_t full;          // x where the change has finished
} wire_cal_t;

typedef enum {
    WC_OK,
    WC_TOO_FEW,             // fewer than WC_MIN_POINTS points
    WC_NO_TRANSITION        // curve stayed flat: never reached the transition
} wc_status_t;

#define WC_MIN_POINTS 8U

// wirecal_analyze — x[i] ascending, r[i] the resistance at x[i]. On
// WC_OK, *out is filled and valid. *open_ended is set when the curve was
// still changing at its last point (the wire has not finished contracting
// within the data), in which case `full` is just the last x.
wc_status_t wirecal_analyze(const uint16_t *x, const uint32_t *r, uint8_t n,
                            wire_cal_t *out, bool *open_ended);

#endif
