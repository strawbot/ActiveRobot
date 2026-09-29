// Host test for wirecal_analyze():  cc -I.. analyze_test.c ../wirecal_analyze.c && ./a.out
#include "wirecal_analyze.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

// Logistic drop of `depth` (fraction of r0) centred at c with width w.
static void curve(uint16_t *x, uint32_t *r, int n, uint32_t r0, double depth, double c, double w, int noise) {
    srand(1);
    for (int i = 0; i < n; i++) {
        x[i] = (uint16_t)(75 + 15 * i);   // duty permille: 15..100 % of a 50 % slot
        double lg = 1.0 / (1.0 + exp(-(x[i] - c) / w));
        r[i] = (uint32_t)(r0 * (1.0 - depth * lg)) + (noise ? (uint32_t)(rand() % (2 * noise + 1)) - noise : 0);
    }
}

int main(void) {
    uint16_t p[64]; uint32_t r[64]; wire_cal_t c; bool open;

    // Clean transition centred at 50 %, finishes well inside the data.
    curve(p, r, 29, 5000, 0.04, 250, 15, 0);
    CHECK(wirecal_analyze(p, r, 29, &c, &open) == WC_OK, "clean: status");
    printf("clean : rcold %u onset %u mid %u full %u open %d\n", c.rcold, c.onset, c.mid, c.full, open);
    CHECK(c.rcold > 4990 && c.rcold < 5010, "clean: rcold");
    CHECK(c.mid >= 240 && c.mid <= 260, "clean: mid %u", c.mid);
    CHECK(c.onset < c.mid && c.mid < c.full, "clean: ordering");
    CHECK(!open, "clean: not open ended");

    // Same with +-6 mohm noise (0.12 %).
    curve(p, r, 29, 5000, 0.04, 250, 15, 6);
    CHECK(wirecal_analyze(p, r, 29, &c, &open) == WC_OK, "noisy: status");
    printf("noisy : rcold %u onset %u mid %u full %u open %d\n", c.rcold, c.onset, c.mid, c.full, open);
    CHECK(c.mid >= 230 && c.mid <= 270, "noisy: mid %u", c.mid);

    // Rising resistance (R-phase style) is found by slope magnitude too.
    curve(p, r, 29, 5000, -0.03, 225, 15, 0);
    CHECK(wirecal_analyze(p, r, 29, &c, &open) == WC_OK && c.mid >= 215 && c.mid <= 235, "rising: mid %u", c.mid);

    // Data ends mid-transition: open ended, full = last activation.
    curve(p, r, 12, 5000, 0.04, 250, 15, 0);    // stops at x = 240
    CHECK(wirecal_analyze(p, r, 12, &c, &open) == WC_OK && open && c.full == p[11], "open ended");

    // Never reaches the transition.
    curve(p, r, 29, 5000, 0.0, 250, 15, 0);
    CHECK(wirecal_analyze(p, r, 29, &c, &open) == WC_NO_TRANSITION, "flat: no transition");

    // Too few points.
    CHECK(wirecal_analyze(p, r, 5, &c, &open) == WC_TOO_FEW, "too few");

    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
