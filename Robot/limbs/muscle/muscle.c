// Robot/limbs/muscle/muscle.c — see muscle.h.

#include "muscle.h"
#include "tea.h"
#include "cli.h"
#include "printers.h"

static uint8_t  fitted;                    // limb mask, bit (limb-1)
static uint8_t  act[MUSCLE_COUNT];         // activation %, by channel
static uint16_t win_start[MUSCLE_COUNT];   // last scheduled windows
static uint16_t win_len[MUSCLE_COUNT];
static const muscle_hw_t *hw;
static bool pending;

static bool limb_fitted(uint8_t limb) {   // limb 0-5
    return limb < MUSCLE_LIMBS && (fitted & (1U << limb));
}

static uint8_t fitted_count(void) {
    uint8_t n = 0;
    for (uint8_t l = 0; l < MUSCLE_LIMBS; l++)
        if (limb_fitted(l)) n++;
    return n;
}

// Pack every active muscle's window back to back from the start of the
// frame. At most two muscles per fitted limb are on (antagonist rule), so
// the packed total is at most 2 * fitted * slot <= MUSCLE_FRAME.
static void schedule(void) {
    pending = false;
    uint8_t  n    = fitted_count();
    uint16_t slot = n ? MUSCLE_FRAME / (2U * n) : 0;
    uint16_t at   = 0;

    for (uint8_t ch = 0; ch < MUSCLE_COUNT; ch++) {
        uint16_t len = (uint16_t)((uint32_t)act[ch] * slot / 100U);
        win_start[ch] = len ? at : 0;
        win_len[ch]   = len;
        at += len;
        if (hw) hw->set_window(ch, win_start[ch], len);
    }
    if (hw && hw->commit) hw->commit();
}

// Coalesce: a gait beat sets several muscles; schedule once afterwards.
static void reschedule(void) {
    if (!pending) {
        pending = true;
        later(schedule);
    }
}

void muscle_init(uint8_t limb_mask) {
    fitted = limb_mask & MUSCLE_LIMBS_6;
    namedAction(schedule);
    muscle_relax();
}

uint8_t muscle_set_fitted(uint8_t limb_mask) {
    uint8_t old = fitted;
    fitted = limb_mask & MUSCLE_LIMBS_6;
    for (uint8_t ch = 0; ch < MUSCLE_COUNT; ch++)
        if (!limb_fitted(ch / MUSCLE_PER_LIMB)) act[ch] = 0;
    reschedule();
    return old;
}

void muscle_bind_hw(const muscle_hw_t *ops) {
    hw = ops;
    reschedule();
}

bool muscle_set(uint8_t muscle, uint8_t pct) {
    if (muscle < 1 || muscle > MUSCLE_COUNT) return false;
    uint8_t ch = muscle - 1;
    if (!limb_fitted(ch / MUSCLE_PER_LIMB)) return false;
    if (pct > 100) pct = 100;
    act[ch] = pct;
    if (pct) act[ch ^ 1U] = 0;   // release the antagonist (W1<->W2, W3<->W4)
    reschedule();
    return true;
}

bool limb_set(uint8_t limb, uint8_t muscle, uint8_t pct) {
    if (limb < 1 || limb > MUSCLE_LIMBS) return false;
    if (muscle < 1 || muscle > MUSCLE_PER_LIMB) return false;
    return muscle_set((uint8_t)((limb - 1) * MUSCLE_PER_LIMB + muscle), pct);
}

uint8_t muscle_get(uint8_t muscle) {
    return (muscle >= 1 && muscle <= MUSCLE_COUNT) ? act[muscle - 1] : 0;
}

void muscle_relax(void) {
    for (uint8_t ch = 0; ch < MUSCLE_COUNT; ch++) act[ch] = 0;
    reschedule();
}

// ── Diagnostics ──────────────────────────────────────────────

static const char *const muscle_names[MUSCLE_PER_LIMB] = {
    "fwd ", "back", "lift", "bend"
};

void muscle_show(void) {
    print("\nmuscle frame ");  printDec(MUSCLE_FRAME);
    print("counts, fitted limbs ");
    for (uint8_t l = 0; l < MUSCLE_LIMBS; l++)
        if (limb_fitted(l)) printDec(l + 1U);
    for (uint8_t l = 0; l < MUSCLE_LIMBS; l++) {
        if (!limb_fitted(l)) continue;
        for (uint8_t m = 0; m < MUSCLE_PER_LIMB; m++) {
            uint8_t ch = (uint8_t)(l * MUSCLE_PER_LIMB + m);
            print("\n L");  printDec0(l + 1U);
            print(" ");     print(muscle_names[m]);
            print(" #");    printDec(ch + 1U);
            print("\t");    printDec(act[ch]);
            print("%\t");
            if (win_len[ch]) {
                printDec(win_start[ch]);  print("+ ");  printDec0(win_len[ch]);
            } else {
                print("off");
            }
        }
    }
    printCr();
}

// ── CLI words ────────────────────────────────────────────────

// Stack values are Cells; clamp before narrowing so -1 or 257 can't wrap
// into a valid muscle number or percentage.
static uint8_t arg(Integer v, uint8_t hi) {
    return v < 0 ? 0 : v > hi ? hi : (uint8_t)v;
}

void muscle_cli_set(void) {   // ( pct muscle -- )
    uint8_t muscle = arg((Integer)ret(), 255);
    uint8_t pct    = arg((Integer)ret(), 100);
    if (!muscle_set(muscle, pct)) print("\nmuscle: no such fitted muscle");
}

void muscle_cli_limb(void) {  // ( pct limb muscle -- )
    uint8_t muscle = arg((Integer)ret(), 255);
    uint8_t limb   = arg((Integer)ret(), 255);
    uint8_t pct    = arg((Integer)ret(), 100);
    if (!limb_set(limb, muscle, pct)) print("\nmuscle: no such fitted limb/muscle");
}
