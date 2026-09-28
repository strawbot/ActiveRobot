// Robot/limbs/muscle/muscle.h — muscle addressing, activation and scheduling.
//
// Layers, top to bottom:
//
//   gait / CLI      "limb 3, knee lift, 60 %"   or   "muscle 11, 60 %"
//   muscle (here)   activation table -> one on-window per muscle per frame
//   board shim      realizes each window on the PWM hardware
//
// A muscle is addressed either as 1-24, or as limb 1-6 with muscle 1-4
// (muscle = (limb-1)*4 + m). Activation is 0-100 %. Callers never deal
// with timing; the scheduler turns activations into non-overlapping
// on-windows inside a repeating PWM frame of MUSCLE_FRAME counts, and hands
// each window to the board through muscle_hw_t. The board owns the actual
// frame period and scales counts to its own timer resolution.
//
// Scheduling rules (4-limb case):
//   - Only fitted limbs (the mask given to muscle_init) take activations.
//   - Each limb has two antagonist pairs: SHOULDER_FWD/BACK and
//     KNEE_LIFT/BEND. Activating one side releases the other, so at most
//     two muscles per limb are ever on.
//   - A muscle at 100 % gets MUSCLE_FRAME / (2 * fitted limbs) counts. The
//     windows are packed back to back, so their sum never exceeds the frame
//     and no two wires conduct at the same time — no supply-sag (VIN)
//     compensation or overlap arbitration is needed.
// With 6 limbs the same rule halves each muscle's maximum on-time again;
// allowing windows to overlap (and compensating via VIN_S) is the planned
// later expansion and would replace schedule() in muscle.c.

#ifndef ROBOT_MUSCLE_H
#define ROBOT_MUSCLE_H

#include <stdint.h>
#include <stdbool.h>

#define MUSCLE_LIMBS      6U
#define MUSCLE_PER_LIMB   4U
#define MUSCLE_COUNT      (MUSCLE_LIMBS * MUSCLE_PER_LIMB)   // 24
#define MUSCLE_FRAME      4096U   // counts per PWM frame

// Muscle within a limb (wire holes W1-W4). W1/W2 and W3/W4 are antagonists.
enum {
    SHOULDER_FWD  = 1,   // swing the limb forward (up to 30 deg)
    SHOULDER_BACK = 2,   // swing the limb back (up to 30 deg)
    KNEE_LIFT     = 3,   // pull the lower limb up, away from the ground
    KNEE_BEND     = 4,   // pull the lower limb down/in (0-80 deg)
};

// Fitted-limb masks: bit (limb-1). The 4-limb build leaves the middle
// shift register (limbs 3 and 4) unused, so it is always zero.
#define MUSCLE_LIMBS_4    0x33U   // limbs 1, 2, 5, 6
#define MUSCLE_LIMBS_6    0x3FU

// Board hardware shim. Channels are 0-23 (muscle number - 1). A window of
// length 0 means off. commit may be NULL; when present it is called after
// all 24 windows have been set, so the board can latch them together.
typedef struct {
    void (*set_window)(uint8_t ch, uint16_t start, uint16_t length);
    void (*commit)(void);
} muscle_hw_t;

void muscle_init(uint8_t limb_mask);
void muscle_bind_hw(const muscle_hw_t *ops);

// Returns false for an out-of-range address or a limb that is not fitted.
bool muscle_set(uint8_t muscle, uint8_t pct);              // muscle 1-24
bool limb_set(uint8_t limb, uint8_t muscle, uint8_t pct);  // limb 1-6, muscle 1-4
uint8_t muscle_get(uint8_t muscle);                        // 0 if out of range
void muscle_relax(void);                                   // everything to 0 %

void muscle_show(void);   // print activations and the scheduled windows

// CLI words
void muscle_cli_set(void);     // ( pct muscle -- )
void muscle_cli_limb(void);    // ( pct limb muscle -- )

#endif
