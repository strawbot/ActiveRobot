// Robot/limbs/muscle/muscle_sr.c — see muscle_sr.h.
//
// Schedule: sorted distinct edge times in timer counts, with the pattern that
// starts at each. The edge at time 0 always exists. Window times from the muscle
// scheduler (0..MUSCLE_FRAME) are scaled to timer counts and rounded to a whole
// number of GAP_US, which bounds how close two edges can be: the interrupt needs
// a few microseconds per edge (shifting the next pattern is most of it).
//
// Three schedule buffers: the one the interrupt is running (cur), one built and
// waiting for the next frame (pending), and a free one for the next build. The
// interrupt only ever reads cur and swaps in pending at a frame start; the task
// writes only the free buffer and publishes it inside a short critical section.

#include "muscle_sr.h"
#include "muscle.h"
#include "sr595.h"
#include "tea.h"        // safe()
#include <string.h>

#define MAX_EDGES   (2U * MUSCLE_COUNT + 1U)
#define NONE        0xFFU
#define GAP_US      24U       // edges are a whole number of these apart
#define TOL         2U        // an edge counts as due this many counts early
#define MARGIN      4U        // closer than this to the compare value: do not wait for the event
#define MASK_INVALID 0xFFFFFFFFU

typedef struct {
    uint32_t t[MAX_EDGES];            // edge time, timer counts, ascending, t[0] == 0
    uint32_t mask[MAX_EDGES];         // pattern from t[i] until t[i+1]
    uint16_t win_us[MUSCLE_COUNT];    // each channel's window length
    uint8_t  n;
} sched_t;

static const muscle_sr_timer_t *tm;
static sched_t  sch[3];
static volatile uint8_t cur;                   // the schedule the interrupt runs
static volatile uint8_t pending = NONE;        // built, waiting for the next frame start
static volatile bool    halted;
static volatile muscle_sr_fault_t fault;
static volatile uint32_t frames_seen;          // frame interrupts so far (the self-test watches this)
static volatile bool    selftest, selftest_hit;
static uint8_t          missed;                // consecutive frames that ended with edges unlatched

// What the interrupt is doing.
static volatile uint32_t out_mask;             // latched pattern
static uint32_t shifted;                       // pattern held in the shift register, or MASK_INVALID
static volatile uint8_t  next_edge;            // index of the next edge to latch in this frame
static volatile bool     edge_armed;           // compare A is armed for t[next_edge]
static volatile uint32_t edge_seq;             // counts latched edges; lets a sample detect staleness
static volatile uint32_t last_edge_count;      // timer count at the last edge

static void (*sample_cb)(uint32_t);
static uint32_t sample_delay_counts;
static volatile bool     sample_armed;
static volatile uint32_t sample_seq;

// Staged windows from set_window(), in muscle counts, and the last committed copy.
static uint16_t stage_start[MUSCLE_COUNT], stage_len[MUSCLE_COUNT];
static uint16_t built_start[MUSCLE_COUNT], built_len[MUSCLE_COUNT];

// ── building a schedule (task context) ─────────────────────────

static uint32_t to_counts(uint32_t muscle_counts) {
    uint32_t gap = (uint32_t)((uint64_t)tm->hz * GAP_US / 1000000U);
    if (gap == 0) gap = 1;
    uint32_t c = (uint32_t)((uint64_t)muscle_counts * tm->frame_counts / MUSCLE_FRAME);
    return (c + gap / 2U) / gap * gap;                       // round to a whole gap
}

static void build(sched_t *s) {
    uint32_t ws[MUSCLE_COUNT], we[MUSCLE_COUNT];
    s->n = 0;
    memset(s->win_us, 0, sizeof s->win_us);

    s->t[s->n++] = 0;
    for (uint8_t ch = 0; ch < MUSCLE_COUNT; ch++) {
        ws[ch] = we[ch] = 0;
        if (!stage_len[ch]) continue;
        uint32_t a = to_counts(stage_start[ch]);
        uint32_t b = to_counts((uint32_t)stage_start[ch] + stage_len[ch]);
        if (b > tm->frame_counts) b = tm->frame_counts;
        if (b <= a) continue;                                // rounded away: too short to latch
        ws[ch] = a; we[ch] = b;
        s->win_us[ch] = (uint16_t)((uint64_t)(b - a) * 1000000U / tm->hz);
        for (int k = 0; k < 2; k++) {                        // insert a and b, keeping t[] sorted and distinct
            uint32_t v = k ? b : a;
            if (v == 0 || v >= tm->frame_counts) continue;
            uint8_t i = 0;
            while (i < s->n && s->t[i] < v) i++;
            if (i < s->n && s->t[i] == v) continue;
            for (uint8_t j = s->n; j > i; j--) s->t[j] = s->t[j - 1];
            s->t[i] = v;
            s->n++;
        }
    }
    for (uint8_t i = 0; i < s->n; i++) {                     // the pattern in force from each edge
        uint32_t m = 0;
        for (uint8_t ch = 0; ch < MUSCLE_COUNT; ch++)
            if (we[ch] > ws[ch] && ws[ch] <= s->t[i] && s->t[i] < we[ch]) m |= 1UL << ch;
        s->mask[i] = m;
    }
    uint8_t w = 1;                                           // drop edges that change nothing
    for (uint8_t i = 1; i < s->n; i++) {
        if (s->mask[i] == s->mask[w - 1]) continue;
        s->t[w] = s->t[i]; s->mask[w] = s->mask[i]; w++;
    }
    s->n = w;
}

static void set_window(uint8_t ch, uint16_t start, uint16_t length) {
    if (ch >= MUSCLE_COUNT) return;
    stage_start[ch] = start;
    stage_len[ch]   = length;
}

static void commit(void) {
    if (!tm || halted) return;
    if (!memcmp(stage_start, built_start, sizeof stage_start) &&
        !memcmp(stage_len,   built_len,   sizeof stage_len)) return;

    uint8_t b = 0;
    uint8_t c, p;
    safe(                                                    // snapshot what is running and waiting
        c = cur;
        p = pending;
    )
    while (b == c || b == p) b++;                            // the third buffer is free whatever the ISR does next
    build(&sch[b]);
    safe(                                                    // publish; a still-waiting older build is superseded
        pending = b;
    )
    memcpy(built_start, stage_start, sizeof built_start);
    memcpy(built_len,   stage_len,   sizeof built_len);
}

// ── the interrupt side ─────────────────────────────────────────

// Stop for good: no more interrupts, chain cleared. Safe from an interrupt.
static void halt_with(muscle_sr_fault_t why) {
    halted = true;
    fault = why;
    if (tm && tm->stop) tm->stop();
    sr595_all_off();
    out_mask = 0;
}

static void arm_sample_for(uint32_t edge_count) {
    if (!sample_cb) return;
    uint32_t at = edge_count + sample_delay_counts;
    if (at >= tm->frame_counts) return;                      // would fall after the wrap
    sample_seq = edge_seq;
    sample_armed = true;
    tm->arm_sample(at);
}

// Latch pattern m now. Its bits were shifted in ahead of time unless something
// else (a manual sr!) has used the shift register since.
static void latch(uint32_t m, uint32_t edge_count) {
    if (sr595_take_dirty()) shifted = MASK_INVALID;
    if (shifted != m) sr595_shift_mask(m);
    sr595_strobe();
    sr595_note_latched(m);
    out_mask = m;
    shifted = m;
    last_edge_count = edge_count;
    edge_seq++;
    arm_sample_for(edge_count);
}

static void preshift(uint32_t m) {
    sr595_shift_mask(m);
    shifted = m;
}

void muscle_sr_isr_frame(void) {
    if (!tm || halted) return;
    frames_seen++;

    // The previous frame should have latched all its edges. If edge_armed is still
    // set, some were not: tolerate one (a delayed interrupt), not two in a row,
    // which means the compare interrupt is not doing its job.
    if (edge_armed) {
        if (++missed >= 2) { halt_with(MUSCLE_SR_FAULT_EDGES_MISSED); return; }
    } else {
        missed = 0;
    }

    uint8_t p = pending;
    if (p != NONE) { cur = p; pending = NONE; }
    const sched_t *s = &sch[cur];

    if (s->mask[0] != out_mask || shifted == MASK_INVALID) latch(s->mask[0], 0);
    else { last_edge_count = 0; edge_seq++; arm_sample_for(0); }

    next_edge = 1;
    if (s->n > 1) { edge_armed = true; tm->arm_edge(s->t[1]); }   // arm first: the shift below takes a few microseconds
    else edge_armed = false;
    preshift(s->n > 1 ? s->mask[1] : s->mask[0]);
}

void muscle_sr_isr_edge(void) {
    if (selftest) { selftest_hit = true; return; }
    if (!tm || halted || !edge_armed) return;
    for (;;) {
        const sched_t *s = &sch[cur];
        uint8_t i = next_edge;
        if (i >= s->n) { edge_armed = false; return; }
        uint32_t now = tm->count();
        if (now + TOL < s->t[i]) { tm->arm_edge(s->t[i]); return; }   // early or stale event: wait for the real one

        latch(s->mask[i], s->t[i]);
        next_edge = (uint8_t)(i + 1U);
        if (next_edge >= s->n) {                             // last edge of the frame
            edge_armed = false;
            preshift(s->mask[0]);                            // the frame-start latch is ready
            return;
        }
        tm->arm_edge(s->t[next_edge]);                       // arm first: the shift below takes a few microseconds
        preshift(s->mask[next_edge]);
        if (tm->count() + MARGIN < s->t[next_edge]) return;  // comfortably ahead: the compare event will come
        // otherwise it is due now (or the compare may already be missed): take it in this pass
    }
}

void muscle_sr_isr_sample(void) {
    if (!tm || halted || !sample_armed) return;
    sample_armed = false;
    if (sample_seq != edge_seq || !sample_cb) return;        // a newer edge has taken over
    sample_cb(out_mask);
}

// ── everything else ────────────────────────────────────────────

void muscle_sr_emergency_stop(void) { halt_with(MUSCLE_SR_FAULT_STOPPED); }

muscle_sr_fault_t muscle_sr_fault(void) { return fault; }

bool muscle_sr_is_on(uint8_t ch) { return ch < MUSCLE_COUNT && ((out_mask >> ch) & 1U); }
uint32_t muscle_sr_mask(void) { return out_mask; }
uint32_t muscle_sr_window_us(uint8_t ch) { return ch < MUSCLE_COUNT ? sch[cur].win_us[ch] : 0; }

uint32_t muscle_sr_edge_age_us(void) {
    if (!tm) return 0;
    uint32_t now = tm->count(), last = last_edge_count;
    uint32_t d = now >= last ? now - last : now + tm->frame_counts - last;
    return (uint32_t)((uint64_t)d * 1000000U / tm->hz);
}

void muscle_sr_on_sample(void (*cb)(uint32_t mask), uint32_t delay_us) {
    sample_cb = NULL;
    if (!tm) return;
    sample_delay_counts = (uint32_t)((uint64_t)tm->hz * delay_us / 1000000U);
    if (sample_delay_counts < 2) sample_delay_counts = 2;
    sample_cb = cb;
}

void muscle_sr_bind_timer(const muscle_sr_timer_t *t) { tm = t; }

static const muscle_hw_t ops = { set_window, commit };

// Wait up to about three frames for *flag to become true, by watching the timer count.
static bool wait_for(volatile bool *flag) {
    uint32_t elapsed = 0, last = tm->count(), limit = 3U * tm->frame_counts;
    while (!*flag && elapsed < limit) {
        uint32_t c = tm->count();
        elapsed += c >= last ? c - last : c + tm->frame_counts - last;
        last = c;
    }
    return *flag;
}

void muscle_sr_init(void) {
    if (!tm) return;
    sch[0].n = 1; sch[0].t[0] = 0; sch[0].mask[0] = 0;       // idle: everything off
    cur = 0; pending = NONE;
    shifted = MASK_INVALID; out_mask = 0;
    halted = false; fault = MUSCLE_SR_OK;
    edge_armed = false; sample_armed = false; missed = 0;

    // Self-test before anything can be latched on: the frame interrupt must fire,
    // and so must a compare match (armed about 1 ms ahead, wrapping if need be).
    uint32_t f0 = frames_seen;
    selftest = true; selftest_hit = false;
    tm->start();
    uint32_t ahead = (uint32_t)((uint64_t)tm->hz / 1000U);
    tm->arm_edge((tm->count() + ahead) % tm->frame_counts);
    bool edge_ok = wait_for(&selftest_hit);
    selftest = false;
    if (!edge_ok) { halt_with(MUSCLE_SR_FAULT_NO_EDGE_IRQ); return; }
    uint32_t start = tm->count(), waited = 0, last = start;
    while (frames_seen == f0 && waited < 3U * tm->frame_counts) {
        uint32_t c = tm->count();
        waited += c >= last ? c - last : c + tm->frame_counts - last;
        last = c;
    }
    if (frames_seen == f0) { halt_with(MUSCLE_SR_FAULT_NO_FRAME_IRQ); return; }

    muscle_bind_hw(&ops);
}
