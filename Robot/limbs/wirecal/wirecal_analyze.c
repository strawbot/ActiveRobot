// Robot/limbs/wirecal/wirecal_analyze.c — see wirecal_analyze.h.
//
// s[i] = r[i+1] - r[i-1] is the two-step slope at interior point i, which
// smooths single-point noise. The steepest |s| is the inflection. Walking out
// from it until |s| falls below a quarter of the peak gives where the change
// starts (onset) and where it has finished (full).

#include "wirecal_analyze.h"

#define PEAK_MIN_PERMILLE 6U    // peak two-step change must be >= 0.6 % of the cold R
#define EDGE_FRACTION     4U    // an edge is where |s| drops below peak / 4

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

wc_status_t wirecal_analyze(const uint16_t *x, const uint32_t *r, uint8_t n,
                            wire_cal_t *out, bool *open_ended) {
    if (n < WC_MIN_POINTS) return WC_TOO_FEW;

    uint32_t r0 = (r[0] + r[1] + r[2]) / 3U;

    int32_t s[256];
    if (n > 250U) n = 250U;
    for (uint8_t i = 1; i + 1 < n; i++) s[i] = (int32_t)r[i + 1] - (int32_t)r[i - 1];

    uint8_t mid = 1;
    for (uint8_t i = 2; i + 1 < n; i++)
        if (iabs(s[i]) > iabs(s[mid])) mid = i;
    int32_t peak = iabs(s[mid]);
    if ((uint32_t)peak * 1000U < r0 * PEAK_MIN_PERMILLE) return WC_NO_TRANSITION;

    int32_t edge = peak / (int32_t)EDGE_FRACTION;

    uint8_t lo = mid;
    while (lo > 1 && iabs(s[lo - 1]) >= edge) lo--;
    uint8_t hi = mid;
    while (hi + 2 < n && iabs(s[hi + 1]) >= edge) hi++;

    // Still moving at the last interior point: the data ended mid-transition.
    bool open = (hi + 2 == n) && iabs(s[hi]) >= edge;

    out->valid = true;
    out->rcold = r0;
    out->onset = x[lo];
    out->mid   = x[mid];
    out->full  = open ? x[n - 1] : x[hi];
    if (open_ended) *open_ended = open;
    return WC_OK;
}
