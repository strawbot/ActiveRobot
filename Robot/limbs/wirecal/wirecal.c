// Robot/limbs/wirecal/wirecal.c — see wirecal.h.
//
// State machine on the TEA scheduler (nothing blocks):
//   REST     the wire cools with everything off
//   SETTLE   activation set; wait WC_DWELL_MS for the temperature to settle
//   MEASURE  each time the wire's window opens, sample WC_SAMPLE_US into it;
//            after WC_SAMPLES samples take the median resistance, then step up
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
static state_t  state;
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
static void take_sample(void);

static uint32_t median(uint32_t *a, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {             // insertion sort, n is tiny
        uint32_t v = a[i]; uint8_t j = i;
        while (j && a[j - 1] > v) { a[j] = a[j - 1]; j--; }
        a[j] = v;
    }
    return a[n / 2];
}

static void finish(void) {
    stop(step_begin_measure); stop(measure_timeout); stop(take_sample);
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

// Sampling happens WC_SAMPLE_US after this wire's window opens, so the current
// has settled and the ADC has converted at least once inside the window.
static void take_sample(void) {
    if (state != MEASURE || !muscle_sr_is_on(ch)) return;   // window already closed
    uint32_t ma, vin;
    if (!hw->sample(ch, &ma, &vin)) return;
    if (ma < WC_MIN_ON_MA) { abort_with("no current while on (no wire, open circuit, or no sense tap)"); return; }
    if (ma > WC_MAX_ON_MA) { abort_with("over current (short circuit or wrong wire)"); return; }
    uint32_t drop = ma * hw->series_mohm;                   // mV*1000 across the series parts
    uint32_t vin_k = vin * 1000U;
    if (vin_k <= drop) { abort_with("supply reading below the series drop"); return; }
    samp_i[nsamp] = ma;
    samp_r[nsamp] = (vin_k - drop) / ma;                    // milliohm
    if (++nsamp >= WC_SAMPLES) step_done();
}

static void on_edge(uint32_t mask) {
    if (state == MEASURE && ((mask >> ch) & 1U)) in(usec(WC_SAMPLE_US), take_sample);
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
    namedAction(step_begin_measure); namedAction(measure_timeout); namedAction(take_sample);
    muscle_sr_on_edge(on_edge);
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
    print("muscle  R cold (mohm)  duty onset  mid  full"); printCr();
    bool any = false;
    for (uint8_t m = 0; m < MUSCLE_COUNT; m++) {
        if (!table[m].valid) continue;
        any = true;
        printDec(m + 1U); print("\t"); printDec0(table[m].rcold);
        print("\t"); print_duty(table[m].onset); print("\t");
        print_duty(table[m].mid); print("\t"); print_duty(table[m].full); printCr();
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
