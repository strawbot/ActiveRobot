// Robot/limbs/gait/gait.c — see gait.h.

#include <math.h>
#include <string.h>
#include "gait.h"
#include "muscle.h"
#include "posture.h"
#include "tea.h"
#include "cli.h"
#include "printers.h"

#define LEGS 4

#define COUPLING     2.0f    // how hard a leg is pulled toward its offset
#define REACH_EXTRA  0.6f    // extra knee drive when reaching for ground
#define SLEW         0.15f   // max change of a signed command per tick

// Leveling: extra stance-knee activation per degree a leg's corner sits
// below level (negative for a corner above level), and its limit.
#define LEVEL_GAIN   2.0f    // % per degree
#define LEVEL_MAX    30.0f   // %

// Logical leg -> physical limb. The middle limbs (3, 4) are not fitted in
// the 4-limb build. Which physical limb is which corner is a mechanical
// choice; change it here.
typedef struct {
    const char *name;
    uint8_t     limb;
    bool        mirror;   // rear legs: forward/back muscles swapped
    int8_t      side;     // +1 right, -1 left
} leg_t;

static const leg_t legs[LEGS] = {
    { "LF", 1, false, -1 },
    { "RF", 2, false, +1 },
    { "LR", 5, true,  -1 },
    { "RR", 6, true,  +1 },
};

typedef struct {
    const char *name;
    float   offset[LEGS];   // LF, RF, LR, RR; larger offset swings earlier
    float   duty;           // stance fraction of the cycle
    uint8_t support;        // legs that must stay down when one lifts
} gait_t;

static const gait_t gaits[] = {
    // One leg in the air at a time: LR, LF, RR, RF.
    { "crawl", { 0.50f, 0.00f, 0.75f, 0.25f }, 0.75f, 3 },
    // Diagonal pairs swing together.
    { "trot",  { 0.00f, 0.50f, 0.50f, 0.00f }, 0.50f, 2 },
    // Same-side pairs swing together.
    { "pace",  { 0.00f, 0.50f, 0.00f, 0.50f }, 0.50f, 2 },
    // Front pair, then rear pair.
    { "bound", { 0.00f, 0.00f, 0.50f, 0.50f }, 0.50f, 2 },
};
#define GAITS (sizeof gaits / sizeof gaits[0])

typedef enum { STANCE, WAIT, SWING, REACH } leg_mode_t;
static const char *const mode_names[] = { "stance", "wait", "swing", "reach" };

typedef struct {
    leg_mode_t mode;
    float      phase;       // 0-1: stance [0, duty), swing [duty, 1)
    uint16_t   reach_ms;
    bool       lifted;      // foot seen off the ground during this swing
    float      shoulder;    // signed command: + foot forward, - back
    float      knee;        // signed command: + bend (1 = support), - lift
} leg_state_t;

static const gait_t *gait;   // NULL when halted
static leg_state_t st[LEGS];
static float    master;      // shared cycle clock, 0-1
static uint8_t  speed   = 50;
static uint8_t  effort  = 70;    // leaves headroom for leveling and reaching
static bool     trace;
static bool     level = true;
static gait_contact_fn contact;

static float wrap(float x) { return x - floorf(x); }

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

// sin(pi * x) for x in [-1, 1] (Bhaskara's approximation, within 0.2 %) —
// keeps newlib's full sinf/cosf and their reduction tables out of the image.
static float sinpi(float x) {
    float a = fabsf(x), p = a * (1.0f - a);
    float y = 16.0f * p / (5.0f - 4.0f * p);
    return x < 0.0f ? -y : y;
}

static float slew(float from, float to) {
    return from + clampf(to - from, -SLEW, SLEW);
}

static uint8_t pct(float x) {   // 0-100 float -> rounded %
    return x <= 0.0f ? 0 : x >= 100.0f ? 100 : (uint8_t)(x + 0.5f);
}

static void event(uint8_t i, const char *what) {
    if (trace) { print("\n");  print(legs[i].name);  print(" ");  print(what); }
}

// ── Feedback ─────────────────────────────────────────────────

// Degrees this leg's corner sits below level: roll > 0 drops the right
// side, pitch > 0 drops the front.
static float corner_drop(const leg_t *leg) {
    int8_t end = leg->mirror ? -1 : +1;
    return leg->side * posture_roll() + end * posture_pitch();
}

// Stance knee activation: a low corner pushes harder, a high one softer.
static float stance_effort(const leg_t *leg) {
    if (!level || !posture_valid()) return effort;
    float c = clampf(LEVEL_GAIN * corner_drop(leg), -LEVEL_MAX, LEVEL_MAX);
    return clampf(effort + c, 0.0f, 100.0f);
}

static bool steady(void) {
    if (!posture_valid()) return true;
    return fabsf(posture_roll()) < GAIT_STEADY_DEG
        && fabsf(posture_pitch()) < GAIT_STEADY_DEG;
}

static uint8_t legs_down(void) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < LEGS; i++)
        if (st[i].mode == STANCE || st[i].mode == WAIT) n++;
    return n;
}

static bool can_lift(void) {
    return legs_down() - 1 >= gait->support && steady();
}

// Stance starts from wherever the shoulder is, so an early landing shortens
// the stride instead of dragging the planted foot.
static void touchdown(uint8_t i) {
    st[i].mode  = STANCE;
    st[i].phase = clampf(0.5f * (1.0f - st[i].shoulder), 0.0f, 0.9f) * gait->duty;
    event(i, "down");
}

// ── Per-leg phase and commands ───────────────────────────────

static void step_leg(uint8_t i, float hz, float dt) {
    leg_state_t *s = &st[i];
    float duty = gait->duty;
    float err  = wrap(master + gait->offset[i] - s->phase + 0.5f) - 0.5f;
    float rate = hz * clampf(1.0f + COUPLING * err, 0.0f, 2.0f);
    bool  foot = contact && contact(i);

    switch (s->mode) {
    case STANCE:
    case WAIT:
        s->phase += rate * dt;
        if (s->phase >= duty) {
            s->phase = duty;
            if (can_lift()) {
                s->mode   = SWING;
                s->lifted = false;
                event(i, "up");
            } else if (s->mode != WAIT) {
                s->mode = WAIT;
                event(i, "wait");
            }
        }
        break;
    case SWING:
        s->phase += fmaxf(rate, (1.0f - duty) * 1000.0f / GAIT_SWING_MS) * dt;
        if (!foot) s->lifted = true;            // wires heat slowly: the foot
                                                // may still drag early in swing
        if (foot && s->lifted && s->phase > duty + 0.5f * (1.0f - duty)) {
            touchdown(i);                       // early: ground came up to meet it
        } else if (s->phase >= 1.0f) {
            s->phase = 1.0f;
            if (!contact || foot) {
                touchdown(i);
            } else {
                s->mode = REACH;
                s->reach_ms = 0;
                event(i, "reach");
            }
        }
        break;
    case REACH:
        s->reach_ms += GAIT_TICK_MS;
        if (foot || s->reach_ms >= GAIT_REACH_MS) touchdown(i);
        break;
    }

    // Signed targets from the phase.
    float shoulder, knee;
    if (s->mode == REACH) {
        shoulder = 1.0f;
        knee     = 1.0f + REACH_EXTRA * s->reach_ms / GAIT_REACH_MS;  // search lower
    } else if (s->phase < duty) {
        shoulder = 1.0f - 2.0f * s->phase / duty;
        knee     = 1.0f;                            // support
    } else {
        float sw = (s->phase - duty) / (1.0f - duty);
        shoulder = -sinpi(0.5f - sw);               // -cos(pi * sw)
        knee     = 1.0f - 2.0f * sinpi(sw);         // support -> lifted -> support
    }
    s->shoulder = slew(s->shoulder, shoulder);
    s->knee     = slew(s->knee, knee);
}

// Drive one antagonist pair from a signed command, zeroing the idle side
// first so muscle_set's antagonist release never fights the active side.
static void drive(uint8_t limb, uint8_t pos, uint8_t neg, uint8_t pos_pct, uint8_t neg_pct) {
    if (pos_pct) { limb_set(limb, neg, 0);  limb_set(limb, pos, pos_pct); }
    else         { limb_set(limb, pos, 0);  limb_set(limb, neg, neg_pct); }
}

static void drive_leg(uint8_t i) {
    const leg_t *leg = &legs[i];
    const leg_state_t *s = &st[i];
    uint8_t fwd  = leg->mirror ? SHOULDER_BACK : SHOULDER_FWD;
    uint8_t back = leg->mirror ? SHOULDER_FWD  : SHOULDER_BACK;
    drive(leg->limb, fwd, back,
          pct(effort * fmaxf(s->shoulder, 0.0f)),
          pct(effort * fmaxf(-s->shoulder, 0.0f)));
    drive(leg->limb, KNEE_BEND, KNEE_LIFT,
          pct(stance_effort(leg) * fmaxf(s->knee, 0.0f)),
          pct(effort * fmaxf(-s->knee, 0.0f)));
}

static void gait_tick(void) {
    if (!gait) return;
    float dt = GAIT_TICK_MS / 1000.0f;
    float hz = GAIT_MAX_HZ * speed / 100.0f;
    master = wrap(master + hz * dt);
    for (uint8_t i = 0; i < LEGS; i++) {
        step_leg(i, hz, dt);
        drive_leg(i);
    }
    after(msec(GAIT_TICK_MS), gait_tick);
}

static void gait_tipped(void) {
    if (!gait) return;
    gait_stop();
    print("\ngait: tipped over, halted");
}

// ── Public API ───────────────────────────────────────────────

void gait_init(void) {
    namedAction(gait_tick);
    posture_on_tip(gait_tipped);
}

void gait_bind_contact(gait_contact_fn fn) { contact = fn; }

// Start (or switch) gait. Legs keep their current phase and are pulled to
// the new offsets by the coupling, so a switch mid-walk is smooth.
bool gait_start(const char *name) {
    for (uint8_t g = 0; g < GAITS; g++) {
        if (strcmp(gaits[g].name, name) != 0) continue;
        if (!gait) {
            master = 0.0f;
            for (uint8_t i = 0; i < LEGS; i++) {
                // Commands start relaxed; the slew limit raises them smoothly.
                float phase = fminf(gaits[g].offset[i], gaits[g].duty);
                st[i] = (leg_state_t){ STANCE, phase, 0, false, 0.0f, 0.0f };
            }
        }
        gait = &gaits[g];
        stop(gait_tick);
        later(gait_tick);
        return true;
    }
    return false;
}

void gait_stop(void) {
    gait = NULL;
    stop(gait_tick);
    muscle_relax();
}

void gait_set_speed(uint8_t p)  { speed  = p > 100 ? 100 : p; }
void gait_set_effort(uint8_t p) { effort = p > 100 ? 100 : p; }

void gait_show(void) {
    print("\ngait ");      print(gait ? gait->name : "halted");
    print("  speed ");     printDec(speed);   print("%");
    print("  effort ");    printDec(effort);  print("%");
    print("  trace ");     print(trace ? "on" : "off");
    print("  level ");     print(level ? "on" : "off");
    print("  contact ");   print(contact ? "sensed" : "assumed");
    posture_show();
    if (gait) {
        for (uint8_t i = 0; i < LEGS; i++) {
            print(" ");  print(legs[i].name);  print(" ");
            print(mode_names[st[i].mode]);  print(" ");
            printDec((unsigned)(st[i].phase * 100.0f));  print("% ");
        }
        printCr();
    }
    for (uint8_t g = 0; g < GAITS; g++) {
        print("\n ");  print(gaits[g].name);
        print("\tduty ");  printDec((unsigned)(gaits[g].duty * 100.0f));
        print("% support ");  printDec(gaits[g].support);
        print(" offsets");
        for (uint8_t i = 0; i < LEGS; i++) {
            print(" ");  print(legs[i].name);  print(":");
            printDec0((unsigned)(gaits[g].offset[i] * 100.0f));
        }
    }
    printCr();
}

// ── CLI words ────────────────────────────────────────────────

void gait_cli_crawl(void) { gait_start("crawl"); }
void gait_cli_trot(void)  { gait_start("trot"); }
void gait_cli_pace(void)  { gait_start("pace"); }
void gait_cli_bound(void) { gait_start("bound"); }

void gait_cli_speed(void) {    // ( pct -- )
    Integer p = (Integer)ret();
    gait_set_speed(p < 0 ? 0 : p > 100 ? 100 : (uint8_t)p);
}

void gait_cli_effort(void) {   // ( pct -- )
    Integer p = (Integer)ret();
    gait_set_effort(p < 0 ? 0 : p > 100 ? 100 : (uint8_t)p);
}

void gait_cli_trace(void) { trace = !trace; }
void gait_cli_level(void) { level = !level; }
