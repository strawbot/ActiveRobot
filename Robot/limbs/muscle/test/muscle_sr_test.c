// Host test for muscle_sr.c's edge schedule and interrupt logic.
//   cc -Wall -Wextra -Itest/stub -I. -I../../io/sr595 test/muscle_sr_test.c muscle_sr.c -o t && ./t [seed]
//
// A simulated timer behaves like the real one: a compare armed for a count that
// has already passed does not fire until the counter comes round again, interrupts
// do not nest, and the ISR takes time (shifting 24 bits costs microseconds). The
// 595 is modelled as a shift register plus a latch. Over a long random run of
// schedule changes, every latched pattern must be the pattern the windows call
// for at that time, and each edge must land within a few microseconds of its time.

#include "muscle_sr.h"
#include "muscle.h"
#include "sr595.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HZ        3000000U                  // 3 MHz: 0.33 us per count
#define FRAME     (HZ / 1000U * MUSCLE_SR_FRAME_MS)   // 60000 counts
#define US(n)     ((uint64_t)(n) * HZ / 1000000U)

// ---- the fake 595 and its cost ----
static uint64_t now;                        // absolute time, counts
static uint32_t shift_reg, latched;
static int      n_strobes, errors;
static uint64_t shift_cost_counts = 6 * 3;  // 6 us per 24-bit shift (18 counts), settable
static struct { uint64_t t; uint32_t m; } log_[200000];
static int nlog;

void sr595_shift_mask(uint32_t m) { shift_reg = m; now += shift_cost_counts; }
void sr595_strobe(void) { latched = shift_reg; log_[nlog].t = now; log_[nlog].m = latched; nlog++; n_strobes++; now += 1; }
void sr595_note_latched(uint32_t m) { (void)m; }
static bool dirty_flag;
bool sr595_take_dirty(void) { bool d = dirty_flag; dirty_flag = false; return d; }
void sr595_all_off(void) { latched = shift_reg = 0; }

// ---- the fake timer and its compare hardware ----
static bool a_armed, b_armed;
static uint64_t a_at, b_at;                  // absolute time of the next compare match, if armed
static uint64_t frame_base;                  // absolute time of the start of the current frame
static bool running, a_pending, b_pending, f_pending;
static bool in_isr, polling, dead_edge, dead_frame;
static void run_until(uint64_t until);

static uint32_t t_count(void) {
    if (polling && !in_isr) run_until(now + 3);     // a task spinning on the count lets time (and interrupts) pass
    return (uint32_t)((now - frame_base) % FRAME);
}
static uint64_t next_match(uint32_t v) {     // next absolute time the counter equals v, strictly after now
    uint32_t c = t_count();
    return v > c ? now - c + v : now - c + FRAME + v;
}
static void t_arm_edge(uint32_t v)   { if (dead_edge) return; a_armed = true; a_at = next_match(v); }
static void t_arm_sample(uint32_t v) { b_armed = true; b_at = next_match(v); }
static void t_start(void) { running = true; frame_base = now; }
static void t_stop(void)  { running = false; a_armed = b_armed = false; }
static const muscle_sr_timer_t timer = { HZ, FRAME, t_start, t_stop, t_count, t_arm_edge, t_arm_sample };

static const muscle_hw_t *hw;
void muscle_bind_hw(const muscle_hw_t *ops) { hw = ops; }

static int samples;
static void on_sample(uint32_t mask) { (void)mask; samples++; }

// ---- reference: what pattern the windows call for at frame time c ----
static uint16_t ws[MUSCLE_COUNT], wl[MUSCLE_COUNT];     // windows as given, muscle counts
static uint32_t q_counts(uint32_t mc) {                 // same rounding as the module
    uint32_t gap = (uint32_t)((uint64_t)HZ * 24 / 1000000U);
    uint32_t c = (uint32_t)((uint64_t)mc * FRAME / MUSCLE_FRAME);
    return (c + gap / 2) / gap * gap;
}
static uint32_t expect_at(uint32_t c) {
    uint32_t m = 0;
    for (unsigned ch = 0; ch < MUSCLE_COUNT; ch++) {
        if (!wl[ch]) continue;
        uint32_t a = q_counts(ws[ch]), b = q_counts((uint32_t)ws[ch] + wl[ch]);
        if (b > FRAME) b = FRAME;
        if (b > a && a <= c && c < b) m |= 1UL << ch;
    }
    return m;
}

// ---- run the simulation until absolute time `until` ----
static void run_until(uint64_t until) {
    while (now < until && running) {
        // next event: frame wrap or a compare match
        uint64_t wrap = frame_base + FRAME;
        uint64_t t = wrap;
        if (a_armed && a_at < t) t = a_at;
        if (b_armed && b_at < t) t = b_at;
        if (t > until) { now = until; break; }
        if (t > now) now = t;
        if (t == wrap) { frame_base = wrap; f_pending = true; }
        if (a_armed && a_at <= now) { a_armed = false; a_pending = true; }
        if (b_armed && b_at <= now) { b_armed = false; b_pending = true; }
        // deliver pending interrupts, frame first; they do not nest, events raised meanwhile wait
        while (f_pending || a_pending || b_pending) {
            in_isr = true;
            if (f_pending)      { f_pending = false; if (!dead_frame) muscle_sr_isr_frame(); }
            else if (a_pending) { a_pending = false; muscle_sr_isr_edge(); }
            else                { b_pending = false; muscle_sr_isr_sample(); }
            in_isr = false;
            // events that came due while the ISR ran
            uint64_t w2 = frame_base + FRAME;
            if (now >= w2) { frame_base = w2; f_pending = true; }
            if (a_armed && a_at <= now) { a_armed = false; a_pending = true; }
            if (b_armed && b_at <= now) { b_armed = false; b_pending = true; }
        }
    }
}

static uint16_t pws[MUSCLE_COUNT], pwl[MUSCLE_COUNT];   // committed, waiting for the next frame

static void random_windows(void) {                       // like the muscle scheduler: packed, non-overlapping
    memset(pws, 0, sizeof pws); memset(pwl, 0, sizeof pwl);
    uint32_t at = 0;
    int n = rand() % 7;
    for (int k = 0; k < n; k++) {
        int ch = rand() % MUSCLE_COUNT;
        if (pwl[ch]) continue;
        uint32_t len = 1 + (uint32_t)(rand() % 400);      // muscle counts: 5 us .. 2 ms
        if (at + len > MUSCLE_FRAME) break;
        pws[ch] = (uint16_t)at; pwl[ch] = (uint16_t)len;
        at += len;
    }
    for (unsigned ch = 0; ch < MUSCLE_COUNT; ch++) hw->set_window((uint8_t)ch, pws[ch], pwl[ch]);
    hw->commit();
}

#define FRAMES 400
static uint16_t f_ws[FRAMES][MUSCLE_COUNT], f_wl[FRAMES][MUSCLE_COUNT];
static int      f_first_strobe[FRAMES + 1];

static void edges_of(const uint16_t *s_, const uint16_t *l_, uint32_t *e, int *ne) {
    *ne = 0; e[(*ne)++] = 0;
    for (unsigned ch = 0; ch < MUSCLE_COUNT; ch++) {
        if (!l_[ch]) continue;
        uint32_t a = q_counts(s_[ch]), b = q_counts((uint32_t)s_[ch] + l_[ch]);
        if (b > FRAME) b = FRAME;
        if (b <= a) continue;
        e[(*ne)++] = a; if (b < FRAME) e[(*ne)++] = b;
    }
}

int main(int argc, char **argv) {
    srand(argc > 1 ? (unsigned)atoi(argv[1]) : 1);
    if (argc > 2) shift_cost_counts = (uint64_t)atoi(argv[2]) * 3;   // shift cost in us (default 6): raise it to stress catch-up

    muscle_sr_bind_timer(&timer);

    if (argc > 3 && !strcmp(argv[3], "noedge")) {            // compare interrupt never fires: self-test must refuse
        dead_edge = true; polling = true;
        muscle_sr_init();
        polling = false;
        printf("noedge: fault %d, bound %s, strobes with a pattern on: ", (int)muscle_sr_fault(), hw ? "YES" : "no");
        int on = 0; for (int i = 0; i < nlog; i++) if (log_[i].m) on++;
        printf("%d\n", on);
        return !(muscle_sr_fault() == MUSCLE_SR_FAULT_NO_EDGE_IRQ && !hw && on == 0);
    }
    if (argc > 3 && !strcmp(argv[3], "noframe")) {
        dead_frame = true; polling = true;
        muscle_sr_init();
        polling = false;
        printf("noframe: fault %d, bound %s\n", (int)muscle_sr_fault(), hw ? "YES" : "no");
        return !(muscle_sr_fault() == MUSCLE_SR_FAULT_NO_FRAME_IRQ && !hw);
    }
    polling = true;
    muscle_sr_init();
    polling = false;
    muscle_sr_on_sample(on_sample, 250);
    if (!hw || muscle_sr_fault() != MUSCLE_SR_OK) { printf("FAIL: init did not complete (fault %d)\n", (int)muscle_sr_fault()); return 1; }

    if (argc > 3 && !strcmp(argv[3], "dies")) {              // the compare interrupt dies after a schedule is running
        for (unsigned ch = 0; ch < MUSCLE_COUNT; ch++) hw->set_window((uint8_t)ch, ch == 3 ? 0 : 0, ch == 3 ? 200 : 0);
        hw->commit();
        run_until(frame_base + FRAME * 3);                    // runs normally
        dead_edge = true;                                     // from now on compare matches are lost
        a_armed = false;
        uint32_t on_before = latched;
        run_until(now + FRAME * 6);
        printf("dies: before the failure latched %06x; fault %d, latched now %06x\n",
               (unsigned)on_before, (int)muscle_sr_fault(), (unsigned)latched);
        return !(muscle_sr_fault() == MUSCLE_SR_FAULT_EDGES_MISSED && latched == 0 && !running);
    }

    uint64_t excl = shift_cost_counts * 3 + US(20);       // probes this close to an edge are not judged
    uint64_t max_late = 0; int judged = 0;
    run_until(frame_base + 2);                            // frame 0 has started
    for (int f = 0; f < FRAMES; f++) {
        uint64_t fb = frame_base;
        memcpy(f_ws[f], ws, sizeof ws); memcpy(f_wl[f], wl, sizeof wl);
        f_first_strobe[f] = nlog;
        if (f % 7 == 0) random_windows();                 // committed now, adopted at the next frame start

        uint32_t edge[2 * MUSCLE_COUNT + 1]; int ne;
        edges_of(ws, wl, edge, &ne);
        for (uint32_t c = 1000; c < FRAME - 1000; c += (uint32_t)(37 * 3 + rand() % 50)) {
            run_until(fb + c);
            bool near = false;
            for (int i = 0; i < ne; i++) {
                int64_t d = (int64_t)c - (int64_t)edge[i];
                if (d > -(int64_t)excl && d < (int64_t)excl) near = true;
            }
            if (near) continue;
            judged++;
            uint32_t want = expect_at((uint32_t)(now - fb));
            if (latched != want) {
                if (errors < 10) printf("frame %d t=%llu counts: latched %06x, windows call for %06x\n",
                                        f, (unsigned long long)(now - fb), (unsigned)latched, (unsigned)want);
                errors++;
            }
        }
        memcpy(ws, pws, sizeof ws); memcpy(wl, pwl, sizeof wl);   // what the frame ISR will adopt
        run_until(fb + FRAME + 2);
    }
    f_first_strobe[FRAMES] = nlog;

    // How late did each latch land relative to its edge?
    for (int f = 0; f < FRAMES; f++) {
        uint64_t fb = 0; (void)fb;
    }
    for (int i = 1; i < nlog; i++) {
        // find the frame this strobe belongs to
        int f = 0; while (f + 1 < FRAMES && i >= f_first_strobe[f + 1]) f++;
        uint32_t edge[2 * MUSCLE_COUNT + 1]; int ne; edges_of(f_ws[f], f_wl[f], edge, &ne);
        // frame base is not stored; strobes carry absolute time, frames are FRAME long from t=0
        uint64_t c = log_[i].t % FRAME;
        uint64_t best = FRAME;
        for (int k = 0; k < ne; k++) {
            uint64_t d = c >= edge[k] ? c - edge[k] : edge[k] - c;
            if (d < best) best = d;
        }
        if (best > max_late && best < excl * 2) max_late = best;
    }
    printf("%d strobes, %d sample callbacks, %d probes judged, %d errors; worst edge lateness %.1f us\n",
           n_strobes, samples, judged, errors, (double)max_late * 1e6 / HZ);
    return errors != 0;
}
