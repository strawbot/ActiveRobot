// Robot/limbs/wirecal/wirecal.c — see wirecal.h.
//
// State machine on the TEA scheduler (nothing blocks):
//   REST     the wire cools with everything off
//   SETTLE   activation set; wait WC_DWELL_MS for the temperature to settle
//   MEASURE  the muscle_sr timer interrupt calls on_sample() WC_SAMPLE_US after each
//            edge; it reads the ADC at that instant and queues the reading, and an
//            action does the rest. After WC_SAMPLES samples take the median
//            resistance, then step up
// The ramp ends when the curve shows a finished transition, at WC_MAX_PCT, or
// on any safety trip; the muscle is relaxed at the end in every case.

#include "wirecal.h"
#include "muscle.h"
#include "muscle_sr.h"
#include "calib.h"
#include "tea.h"
#include "cli.h"
#include "printers.h"

static const wirecal_hw_t *hw;
static wire_cal_t table[MUSCLE_COUNT];

typedef enum { IDLE, REST, SETTLE, MEASURE } state_t;
static volatile state_t state;          // read by the sample interrupt
static uint8_t  ch;                 // channel under test (muscle - 1)
static uint8_t  saved_fitted;       // limb mask to restore when the run ends
static uint8_t  cur_pct;
static uint8_t  npts;
static uint16_t pts_x[WC_MAX_POINTS];   // duty, tenths of a percent of the frame
static uint32_t pts_r[WC_MAX_POINTS];
static uint32_t samp_r[WC_SAMPLES];
static uint32_t samp_i[WC_SAMPLES];
static uint8_t  nsamp;
static bool     named;

static void step_begin_measure(void);
static void measure_timeout(void);
static void drain(void);
static void scope_burst(void);

static uint32_t median(uint32_t *a, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {             // insertion sort, n is tiny
        uint32_t v = a[i]; uint8_t j = i;
        while (j && a[j - 1] > v) { a[j] = a[j - 1]; j--; }
        a[j] = v;
    }
    return a[n / 2];
}

static void finish(void) {
    stop(step_begin_measure); stop(measure_timeout);
    muscle_relax();
    muscle_set_fitted(saved_fitted);
    state = IDLE;
}

// Print duty as percent with one decimal, e.g. 75 -> "7.5%".
static void print_duty(uint32_t tenths) {
    printDec0(tenths / 10U); print("."); printDec0(tenths % 10U); print("%");
}

static void abort_with(const char *why) {
    finish();
    print("wirecal: aborted: "); print(why); printCr();
}

static void complete(void) {
    finish();
    wire_cal_t c; bool open = false;
    wc_status_t st = wirecal_analyze(pts_x, pts_r, npts, &c, &open);
    if (st == WC_OK) {
        table[ch] = c;
        print("wirecal: muscle "); printDec0(ch + 1U);
        print("  R cold "); printDec0(c.rcold); print(" mohm");
        print("  duty onset ");  print_duty(c.onset);
        print("  mid ");         print_duty(c.mid);
        print("  full ");        print_duty(c.full);
        if (open) print("  (still moving at the last step: full is a lower bound)");
        printCr();
        calib_save();
    } else {
        print(st == WC_TOO_FEW ? "wirecal: too few points to analyze"
                               : "wirecal: no transition found (flat curve; drive too weak or wrong wire?)");
        printCr();
    }
}

// One measurement step is done: record it and move on.
static void step_done(void) {
    stop(measure_timeout);
    uint32_t r = median(samp_r, nsamp);
    uint32_t i = median(samp_i, nsamp);
    pts_x[npts] = (uint16_t)(cur_pct * WC_DUTY_PER_PCT); pts_r[npts] = r; npts++;

    print("  act "); printDec(cur_pct); print("%  duty ");
    print_duty(cur_pct * WC_DUTY_PER_PCT); print("  I "); printDec0(i);
    print(" mA  R "); printDec0(r); print(" mohm"); printCr();

    // Average current = peak while on x duty. Stop before the wire is driven past
    // its rating; the result so far is still analyzed (and marked open ended).
    if (i * (cur_pct * WC_DUTY_PER_PCT) / 1000U >= WC_MAX_AVG_MA) {
        print("wirecal: stopping at the average-current limit"); printCr();
        complete();
        return;
    }

    wire_cal_t c; bool open = false;
    bool done = wirecal_analyze(pts_x, pts_r, npts, &c, &open) == WC_OK && !open;
    if (done || cur_pct + WC_STEP_PCT > WC_MAX_PCT || npts >= WC_MAX_POINTS) { complete(); return; }

    cur_pct += WC_STEP_PCT;
    muscle_set((uint8_t)(ch + 1U), cur_pct);
    state = SETTLE;
    in(msec(WC_DWELL_MS), step_begin_measure);
}

static void step_begin_measure(void) {
    if (state == REST) {                       // first entry: start the ramp
        cur_pct = WC_START_PCT;
        muscle_set((uint8_t)(ch + 1U), cur_pct);
        state = SETTLE;
        in(msec(WC_DWELL_MS), step_begin_measure);
        return;
    }
    nsamp = 0;
    state = MEASURE;
    in(msec(WC_MEASURE_MS), measure_timeout);
}

static void measure_timeout(void) {
    if (state == MEASURE) abort_with("too few on-window samples (window too short, or no sense tap)");
}

// ── capture in the interrupt, process in an action ───────────
//
// on_sample() runs in the sample interrupt, WC_SAMPLE_US after every edge. All it
// does is read the ADC for whichever wire is of interest and queue the reading;
// drain() does the arithmetic, the checks and any printing in task context.

#define RING 16U
static struct { uint8_t ch; uint16_t ma, vin; } ring[RING];
static volatile uint8_t rhead, rtail;       // head written by the interrupt, tail by drain()
static volatile bool    drain_queued;

static volatile bool monitoring;
static volatile bool scope_armed;
static uint8_t  scope_ch;

static void on_sample(uint32_t mask) {      // interrupt context
    bool pushed = false;
    for (uint8_t c = 0; mask; c++, mask >>= 1) {
        if (!(mask & 1U)) continue;
        if (scope_armed && c == scope_ch) { scope_armed = false; later(scope_burst); }
        bool want = monitoring || (state == MEASURE && c == ch);
        if (!want) continue;
        uint32_t ma, vin;
        if (!hw->sample(c, &ma, &vin)) continue;
        uint8_t h = rhead, nh = (uint8_t)((h + 1U) % RING);
        if (nh == rtail) continue;                          // full: drop the reading
        ring[h].ch = c; ring[h].ma = (uint16_t)ma; ring[h].vin = (uint16_t)vin;
        rhead = nh;
        pushed = true;
    }
    if (pushed && !drain_queued) { drain_queued = true; later(drain); }
}

static void mon_add(uint8_t c, uint32_t ma, uint32_t vin);

// One calibration reading: check it, and store it as a resistance.
static void take_sample(uint32_t ma, uint32_t vin) {
    if (ma < WC_MIN_ON_MA) { abort_with("no current while on (no wire, open circuit, or no sense tap)"); return; }
    if (ma > WC_MAX_ON_MA) { abort_with("over current (short circuit or wrong wire)"); return; }
    uint32_t drop = ma * hw->series_mohm;                   // mV*1000 across the series parts
    uint32_t vin_k = vin * 1000U;
    if (vin_k <= drop) { abort_with("supply reading below the series drop"); return; }
    samp_i[nsamp] = ma;
    samp_r[nsamp] = (vin_k - drop) / ma;                    // milliohm
    if (++nsamp >= WC_SAMPLES) step_done();
}

static void drain(void) {
    drain_queued = false;
    while (rtail != rhead) {
        uint8_t t = rtail;
        uint8_t c = ring[t].ch; uint32_t ma = ring[t].ma, vin = ring[t].vin;
        rtail = (uint8_t)((t + 1U) % RING);
        if (monitoring) mon_add(c, ma, vin);
        if (state == MEASURE && c == ch) take_sample(ma, vin);
    }
}

// ── isns: what the sense taps read while the wires pulse ─────
//
// `status` reads the ADC at an arbitrary moment, which for a pulsed wire is
// usually between pulses. This samples each wire the same way calibration
// does, WC_SAMPLE_US into its own window, over MON_MS, then prints the peak
// current while on and the wire resistance that implies.

#define MON_MS 1000U

typedef struct {
    uint16_t n, min, max, vmin, vmax;
    uint32_t sum_ma, sum_vin, sum_r;
} mon_t;

static mon_t    mon[MUSCLE_COUNT];

static uint8_t clamp8(Cell v, uint8_t hi);

static void mon_add(uint8_t c, uint32_t ma, uint32_t vin) {
    if (ma == 0) return;
    mon_t *m = &mon[c];
    if (!m->n || ma < m->min) m->min = (uint16_t)ma;
    if (ma > m->max) m->max = (uint16_t)ma;
    if (!m->n || vin < m->vmin) m->vmin = (uint16_t)vin;
    if (vin > m->vmax) m->vmax = (uint16_t)vin;
    m->sum_ma += ma; m->sum_vin += vin;
    uint32_t drop = ma * hw->series_mohm, vk = vin * 1000U;
    if (vk > drop) m->sum_r += (vk - drop) / ma;
    m->n++;
}

// Column stops for the isns table.
enum { C_MUSCLE = 0, C_SAMPLES = 8, C_CURRENT = 17, C_R = 36, C_VIN = 46 };

static void mon_report(void) {
    monitoring = false;
    bool any = false;
    printCr();
    tabTo(C_MUSCLE);  print("muscle");
    tabTo(C_SAMPLES); print("samples");
    tabTo(C_CURRENT); print("I mA min/avg/max");
    tabTo(C_R);       print("R mohm");
    tabTo(C_VIN);     print("VIN mV min/avg/max");
    printCr();
    for (uint8_t c = 0; c < MUSCLE_COUNT; c++) {
        const mon_t *m = &mon[c];
        if (!m->n) continue;
        any = true;
        tabTo(C_MUSCLE);  printDec0(c + 1U);
        tabTo(C_SAMPLES); printDec0(m->n);
        tabTo(C_CURRENT); printDec0(m->min); print("/"); printDec0(m->sum_ma / m->n); print("/"); printDec0(m->max);
        tabTo(C_R);       printDec0(m->sum_r / m->n);
        tabTo(C_VIN);     printDec0(m->vmin); print("/"); printDec0(m->sum_vin / m->n); print("/"); printDec0(m->vmax);
        printCr();
    }
    if (!any) {
        print("isns: no samples. Is a wire active (e.g. `100 1 3 limb!`), and does it have a sense tap?");
        printCr();
    }
}

void wirecal_cli_isns(void) {
    if (!hw || monitoring || state != IDLE) { print("isns: busy"); printCr(); return; }
    for (uint8_t c = 0; c < MUSCLE_COUNT; c++) mon[c] = (mon_t){ 0 };
    monitoring = true;
    print("isns: sampling for 1 s"); printCr();
    in(msec(MON_MS), mon_report);
}

// ── scope: the waveform the ADC sees across one pulse ─────────
//
// Waits for the next pulse of one wire, then reads the ADC back to back for as
// long as the wire stays on (up to SCOPE_MAX_US), recording the time since the
// edge, the current and the supply. Compare it with a scope on the sense
// resistor to see whether the ADC sees the flat plateau or something else (an
// edge, ringing, noise). The burst blocks its action, but the timer interrupt
// ends the pulse on time regardless, so it cannot stretch what it measures.

#define SCOPE_POINTS 40U
#define SCOPE_MAX_US 1200U
#define SCOPE_WAIT_MS 2000U
// Results are kept and printed a few rows per scheduler pass, with a gap for the
// serial line to drain: a burst of ~1.5 KB from one action overruns the 400-byte
// emit queue (see Docs/output-backpressure-design.md).
#define SCOPE_ROWS_PER_PASS 5U
#define SCOPE_PRINT_GAP_MS  30U

static struct { uint16_t us, ma, vin; } sp[SCOPE_POINTS];
static uint8_t  sn, sprinted;
static uint32_t s_first[4], s_last[4];
static uint32_t scope_window_us;

static void print_taps(const char *when, const uint32_t t[4]) {
    print("  taps "); print(when); print(" (mV at the pin):");
    for (uint8_t i = 0; i < 4U; i++) { print("  A"); printDec0(i); print(" "); printDec0(t[i]); }
    printCr();
}

static void scope_print(void) {
    if (sprinted == 0) {                                      // heading, taps and column titles first
        printCr();
        print("scope: muscle "); printDec0(scope_ch + 1U); print(", window "); printDec0(scope_window_us);
        print(" us, "); printDec0(sn); print(" samples"); printCr();
        if (hw->taps) { print_taps("at the start", s_first); print_taps("at the end  ", s_last); }
        tabTo(0);  print("us");
        tabTo(8);  print("I mA");
        tabTo(16); print("VIN mV");
        tabTo(26); print("R mohm");
        printCr();
        sprinted = 1;                                         // 1 = heading done; rows are sprinted - 1
        in(msec(SCOPE_PRINT_GAP_MS), scope_print);
        return;
    }
    uint8_t row = (uint8_t)(sprinted - 1U);
    for (uint8_t k = 0; k < SCOPE_ROWS_PER_PASS && row < sn; k++, row++) {
        uint32_t drop = sp[row].ma * hw->series_mohm, vk = sp[row].vin * 1000U;
        tabTo(0);  printDec0(sp[row].us);
        tabTo(8);  printDec0(sp[row].ma);
        tabTo(16); printDec0(sp[row].vin);
        tabTo(26); printDec0(sp[row].ma && vk > drop ? (vk - drop) / sp[row].ma : 0);
        printCr();
    }
    sprinted = (uint8_t)(row + 1U);
    if (row < sn) { in(msec(SCOPE_PRINT_GAP_MS), scope_print); return; }
    if (sn < 4) { print("scope: few samples: the window is too short for a burst, or the ADC is not updating"); printCr(); }
    sprinted = 0;
}

static void scope_burst(void) {
    sn = 0;
    scope_window_us = muscle_sr_window_us(scope_ch);
    if (hw->taps) hw->taps(s_first);                          // every tap, at the start of the pulse
    while (sn < SCOPE_POINTS && muscle_sr_is_on(scope_ch)) {
        uint32_t us = muscle_sr_edge_age_us();
        if (us > SCOPE_MAX_US) break;
        uint32_t ma, vin;
        if (!hw->sample(scope_ch, &ma, &vin)) continue;       // no fresh conversion yet
        sp[sn].us = (uint16_t)us; sp[sn].ma = (uint16_t)ma; sp[sn].vin = (uint16_t)vin;
        sn++;
    }
    if (hw->taps) hw->taps(s_last);                           // and at the end of the burst
    sprinted = 0;
    later(scope_print);                                       // print outside the burst, paced
}

static void scope_giveup(void) {
    if (!scope_armed) return;
    scope_armed = false;
    print("scope: that wire never pulsed (is it active?)"); printCr();
}

void wirecal_cli_scope(void) {   // ( muscle -- )
    uint8_t m = clamp8(ret(), 255);
    if (!hw || scope_armed || state != IDLE || m < 1 || m > MUSCLE_COUNT) {
        print("scope: busy or bad muscle"); printCr();
        return;
    }
    scope_ch = (uint8_t)(m - 1U);
    scope_armed = true;
    in(msec(SCOPE_WAIT_MS), scope_giveup);
}

bool wirecal_running(void) { return state != IDLE; }

bool wirecal_start(uint8_t muscle) {
    if (!hw || state != IDLE || muscle < 1 || muscle > MUSCLE_COUNT) return false;
    ch = (uint8_t)(muscle - 1U);
    if (!muscle_set(muscle, 0)) return false;              // not a fitted muscle
    saved_fitted = muscle_set_fitted((uint8_t)(1U << (ch / MUSCLE_PER_LIMB)));   // this limb alone
    npts = 0;
    state = REST;
    print("wirecal: muscle "); printDec0(muscle); print(": resting, then ramping from ");
    printDec0(WC_START_PCT); print("% in steps of "); printDec0(WC_STEP_PCT);
    print("% (this limb alone gets a 50% slot)"); printCr();
    in(msec(WC_REST_MS), step_begin_measure);
    return true;
}

void wirecal_stop(void) {
    if (state == IDLE) return;
    abort_with("stopped");
}

bool wirecal_get(uint8_t muscle, wire_cal_t *out) {
    if (muscle < 1 || muscle > MUSCLE_COUNT || !table[muscle - 1].valid) return false;
    *out = table[muscle - 1];
    return true;
}

// One line per calibrated wire: "<muscle> <rcold> <onset> <mid> <full> wire!"
// with onset/mid/full as duty in tenths of a percent of the frame.
static void write_wires(calib_line_fn emit) {
    for (uint8_t m = 0; m < MUSCLE_COUNT; m++) {
        if (!table[m].valid) continue;
        int32_t v[5] = { m + 1, (int32_t)table[m].rcold, table[m].onset, table[m].mid, table[m].full };
        emit("wire!", v, 5);
    }
}

void wirecal_bind_hw(const wirecal_hw_t *ops) { hw = ops; }

void wirecal_init(void) {
    if (named) return;
    named = true;
    namedAction(step_begin_measure); namedAction(measure_timeout); namedAction(drain);
    namedAction(mon_report); namedAction(scope_burst); namedAction(scope_giveup); namedAction(scope_print);
    muscle_sr_on_sample(on_sample, WC_SAMPLE_US);
    calib_register_writer(write_wires);
}

// ── CLI words ────────────────────────────────────────────────

static uint16_t clamp16(Cell v, uint16_t hi) { return v < 0 ? 0 : v > hi ? hi : (uint16_t)v; }
static uint8_t  clamp8(Cell v, uint8_t hi)   { return (uint8_t)clamp16(v, hi); }

void wirecal_cli(void) {   // ( muscle -- )
    uint8_t m = clamp8(ret(), 255);
    if (!wirecal_start(m)) { print("wirecal: not started (busy, bad muscle, or not fitted)"); printCr(); }
}

void wirecal_cli_stop(void) { wirecal_stop(); }

void wirecal_cli_show(void) {
    tabTo(0);  print("muscle");
    tabTo(8);  print("R cold mohm");
    tabTo(22); print("onset");
    tabTo(32); print("mid");
    tabTo(42); print("full");
    printCr();
    bool any = false;
    for (uint8_t m = 0; m < MUSCLE_COUNT; m++) {
        if (!table[m].valid) continue;
        any = true;
        tabTo(0);  printDec0(m + 1U);
        tabTo(8);  printDec0(table[m].rcold);
        tabTo(22); print_duty(table[m].onset);
        tabTo(32); print_duty(table[m].mid);
        tabTo(42); print_duty(table[m].full);
        printCr();
    }
    if (!any) { print("(no wire calibrated; run: <muscle> wirecal)"); printCr(); }
}

void wirecal_cli_set(void) {   // ( muscle rcold onset mid full -- )
    uint16_t full  = clamp16(ret(), 1000);
    uint16_t mid   = clamp16(ret(), 1000);
    uint16_t onset = clamp16(ret(), 1000);
    uint32_t rc   = (uint32_t)ret();
    uint8_t m     = clamp8(ret(), 255);
    if (m < 1 || m > MUSCLE_COUNT) return;
    table[m - 1] = (wire_cal_t){ true, rc, onset, mid, full };
}
