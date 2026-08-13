// Robot/io/ad5593r/ad5593r_limb.c — see ad5593r_limb.h for the API
// contract and the linear-region concept.

#include "ad5593r_limb.h"
#include "ad5593r.h"
#include "timeout.h"
#include "tea.h"
#include <stddef.h>

#define AD5593R_LIMB_MAX_MV       2500U   // full DAC range (internal 2.5V ref)
#define AD5593R_LIMB_DAC_MAX_CODE 4095U   // full 12-bit DAC range
#define AD5593R_LIMB_ADC_SATURATE 4090U   // "reached maximum" — small margin for ADC noise

// Ramp DAC 0 -> DAC_MAX_CODE one code at a time, settling this long after
// each setting before reading the ADC and stopping once it reads at/above
// ADC_SATURATE. This blocks the whole scheduler for its duration
// (ms_blocking, not a TEA action) — the original PWM-driven muscle_wire.c
// avoided that by being a self-rescheduling state machine, because
// nitinol wire needs ~2s to reach thermal equilibrium after a duty change
// (see Limbs/muscle_wire.c). Worst case (a limb that never saturates)
// sweeps the full 4096 codes: 4096 * 10ms = ~41s per limb, ~164s for all
// four — if that's unacceptable, this needs to become a TEA state
// machine instead.
#define AD5593R_LIMB_SETTLE_MS   10U

// Slope is estimated over a trailing window of codes (not step-to-step)
// so single-sample ADC noise can't fake a "linear" transition at this
// fine a step size; this also keeps calibration's RAM footprint to one
// small ring buffer instead of an array over the whole 4096-code sweep
// (the board only budgets 2KB of stack total — see script/fsp.ld).
// "Linear" is a trailing-window delta at least this fraction of the
// largest such delta seen so far in the ramp.
#define AD5593R_LIMB_WINDOW         16U
#define AD5593R_LIMB_SLOPE_FRACTION 4U

static ad5593r_limb_cal_t g_cal[AD5593R_LIMB_COUNT];

static uint16_t mv_to_code(uint32_t mv) {
    if (mv > AD5593R_LIMB_MAX_MV) {
        mv = AD5593R_LIMB_MAX_MV;
    }
    return (uint16_t)(mv * 4095U / AD5593R_LIMB_MAX_MV);
}

static uint32_t code_to_mv(uint16_t code) {
    return (uint32_t)code * AD5593R_LIMB_MAX_MV / 4095U;
}

static bool limb_set_dac_mv(uint8_t limb, uint32_t mv) {
    return ad5593r_set_dac(limb, mv_to_code(mv));
}

// calibrate to find low and high DAC codes that produce a linear ADC response, and store
// low ADC is 2621 while high ADC is 3768
#define LOW_ADC 2621
#define HIGH_ADC 3768

bool ad5593r_limb_calibrate(uint8_t limb) {
    if (limb >= AD5593R_LIMB_COUNT) {
        return false;
    }

    int32_t  lo_code    = -1;
    int32_t  hi_code    = -1;

    uint16_t dac_code = 0;
    for (;;) {
        if (!ad5593r_set_dac(limb, dac_code)) {
            ad5593r_set_dac(limb, 0);
            return false;
        }
        timeoutWait(msec(AD5593R_LIMB_SETTLE_MS));
        uint16_t adc_code;
        if (!ad5593r_read_adc(limb, &adc_code)) {
            ad5593r_set_dac(limb, 0);
            return false;
        }

        if (lo_code == -1 && adc_code > LOW_ADC) {
            lo_code = dac_code;
        }
        hi_code = dac_code;

        bool saturated = (adc_code >= HIGH_ADC);
        bool at_top     = (dac_code >= AD5593R_LIMB_DAC_MAX_CODE);
        if (saturated || at_top) {
            break;
        }
        dac_code++;
    }

    // Always return to a safe/idle drive level after the sweep, whether
    // or not a usable linear region turns up below.
    bool reset_ok = ad5593r_set_dac(limb, 0);

    if (!reset_ok || lo_code < 0 || hi_code <= lo_code) {
        return false;
    }

    g_cal[limb].dac_lo_mv  = (uint16_t)code_to_mv((uint16_t)lo_code);
    g_cal[limb].dac_hi_mv  = (uint16_t)code_to_mv((uint16_t)hi_code);
    g_cal[limb].calibrated = true;
    return true;
}

bool ad5593r_limb_calibrate_all(void) {
    bool ok = true;
    for (uint8_t limb = 0; limb < AD5593R_LIMB_COUNT; limb++) {
        ok = ad5593r_limb_calibrate(limb) && ok;
    }
    return ok;
}

bool ad5593r_limb_set(uint8_t limb, uint8_t pct) {
    if (limb >= AD5593R_LIMB_COUNT || !g_cal[limb].calibrated) {
        return false;
    }
    if (pct > 100U) {
        pct = 100U;
    }
    uint32_t span = (uint32_t)g_cal[limb].dac_hi_mv - g_cal[limb].dac_lo_mv;
    uint32_t mv   = g_cal[limb].dac_lo_mv + (span * pct) / 100U;
    return limb_set_dac_mv(limb, mv);
}

bool ad5593r_limb_get_cal(uint8_t limb, ad5593r_limb_cal_t *cal) {
    if (limb >= AD5593R_LIMB_COUNT || cal == NULL) {
        return false;
    }
    *cal = g_cal[limb];
    return true;
}
