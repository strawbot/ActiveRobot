// Robot/limbs/muscle/muscle_sr.c — see muscle_sr.h.
//
// Windows arrive in counts (0..MUSCLE_FRAME) and are converted to scheduler
// ticks. The distinct edge times of the frame are sorted once per change;
// the runner then walks them: at each edge time it computes which windows
// cover that instant, latches that pattern if it differs from what is
// already out, and sleeps until the next edge (or the frame end, then wraps
// to time 0).

#include "muscle_sr.h"
#include "muscle.h"
#include "sr595.h"
#include "tea.h"
#include <string.h>

#define MAX_TIMES (2U * MUSCLE_COUNT + 1U)

static uint32_t ts[MUSCLE_COUNT];   // window start, ticks (staged then live)
static uint32_t te[MUSCLE_COUNT];   // window end, ticks (exclusive)
static uint32_t ns[MUSCLE_COUNT];   // staged by set_window, applied by commit
static uint32_t ne[MUSCLE_COUNT];

static uint32_t times[MAX_TIMES];   // sorted distinct edge times, times[0] == 0
static uint8_t  ntimes;
static uint8_t  idx;
static uint32_t out_mask = 0;       // pattern currently latched
static uint32_t frame_ticks;

static uint32_t to_ticks(uint32_t counts) {
    return counts * frame_ticks / MUSCLE_FRAME;
}

static void set_window(uint8_t ch, uint16_t start, uint16_t length) {
    if (ch >= MUSCLE_COUNT) return;
    ns[ch] = to_ticks(start);
    ne[ch] = length ? to_ticks((uint32_t)start + length) : ns[ch];
}

static uint32_t mask_at(uint32_t t) {
    uint32_t m = 0;
    for (uint8_t ch = 0; ch < MUSCLE_COUNT; ch++)
        if (ts[ch] <= t && t < te[ch]) m |= 1UL << ch;
    return m;
}

static void drive(uint32_t m) {
    if (m == out_mask) return;
    out_mask = m;
    for (uint8_t c = 0; c < SR595_MAX_CHIPS; c++)
        sr595_put(c, (uint8_t)(m >> (8U * c)));
    sr595_latch();
}

static void runner(void) {
    uint32_t t = times[idx];
    drive(mask_at(t));
    uint8_t next = (uint8_t)(idx + 1U);
    uint32_t dt;
    if (next < ntimes) {
        dt = times[next] - t;
    } else {
        dt = frame_ticks - t;
        next = 0;
    }
    idx = next;
    in(dt, runner);
}

static void add_time(uint32_t t) {
    if (t >= frame_ticks) return;       // frame end is the wrap, not an edge
    uint8_t i = 0;
    while (i < ntimes && times[i] < t) i++;
    if (i < ntimes && times[i] == t) return;   // already present
    for (uint8_t j = ntimes; j > i; j--) times[j] = times[j - 1];
    times[i] = t;
    ntimes++;
}

static void commit(void) {
    if (!memcmp(ts, ns, sizeof ts) && !memcmp(te, ne, sizeof te)) return;
    memcpy(ts, ns, sizeof ts);
    memcpy(te, ne, sizeof te);
    stop(runner);

    ntimes = 0;
    for (uint8_t ch = 0; ch < MUSCLE_COUNT; ch++) {
        if (te[ch] <= ts[ch]) { ts[ch] = te[ch] = 0; continue; }   // rounded away
        add_time(ts[ch]);
        add_time(te[ch]);
    }
    if (!ntimes) { drive(0); return; }   // nothing on: outputs off, runner idle
    add_time(0);
    idx = 0;
    runner();
}

static const muscle_hw_t ops = { set_window, commit };

void muscle_sr_init(void) {
    frame_ticks = msec(MUSCLE_SR_FRAME_MS);
    namedAction(runner);
    muscle_bind_hw(&ops);
}
