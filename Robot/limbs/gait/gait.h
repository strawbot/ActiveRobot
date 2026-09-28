// Robot/limbs/gait/gait.h — smooth, feedback-timed walking for the 4-limb build.
//
// Sits on top of Robot/limbs/muscle. Every GAIT_TICK_MS it sets each leg's
// shoulder and knee muscles through limb_set(); the muscle scheduler turns
// those activations into PWM windows. It runs as a self-rescheduling TEA
// action, so it never blocks.
//
// Desire and feedback
//   Desire sets the rhythm: speed (0-100 %) drives a shared cycle clock and
//   each leg is pulled toward its gait's phase offset on that clock. Speed 0
//   stands still (legs keep supporting and leveling).
//   Feedback sets the actual steps:
//     - touchdown: a swinging foot lands when it touches ground again after
//       leaving it (early on a bump). If it reaches the end of its swing without contact it keeps
//       reaching down, up to GAIT_REACH_MS.
//     - lift-off: a foot leaves the ground only if enough other legs are in
//       stance (3 for crawl, 2 for the others) and the body is steady.
//       Otherwise the leg waits and the rhythm catches up later.
//   Without a contact sensor, touchdown happens at the end of the swing.
//   As in animals, swing time stays roughly constant (at most GAIT_SWING_MS)
//   and speed mostly changes how long each foot pushes; a foot that is in
//   the air always finishes its swing, even at speed 0.
//
// Smooth motion
//   Each leg has a continuous phase: stance for [0, duty), swing for
//   [duty, 1). Shoulder and knee commands are smooth functions of it:
//     shoulder  stance: forward -> back at constant speed (body glides)
//               swing:  back -> forward along a cosine
//     knee      stance: held at support (bend = effort, plus leveling)
//               swing:  1 - 2 sin(pi s): support -> lifted -> support, so
//                       the foot arrives at stance height
//               reach:  bend rises above effort to search lower
//   Each antagonist pair is driven from one signed value, so the two sides
//   hand over through zero instead of switching. A slew limit smooths any
//   remaining jump (an early touchdown, a gait change).
//   Rear legs are mounted mirror-image to the front ones (front limbs
//   project 60 deg forward, rear limbs 60 deg back), so their forward/back
//   muscles are swapped.
//
// IMU feedback (Robot/limbs/posture), when a sensor is bound:
//   - leveling: a stance knee gets LEVEL_GAIN % more activation per degree
//     its corner sits below level (less for a corner above level).
//   - steadiness: no lift-off while tilt exceeds GAIT_STEADY_DEG.
//   - tip-over: the gait halts and every muscle is relaxed.
// Call posture_init() before gait_init().

#ifndef ROBOT_GAIT_H
#define ROBOT_GAIT_H

#include <stdint.h>
#include <stdbool.h>

#define GAIT_TICK_MS      20
#define GAIT_MAX_HZ       0.5f    // step cycles per second at 100 % speed
#define GAIT_SWING_MS     500     // longest a swing takes, at any speed
#define GAIT_REACH_MS     1000    // longest a foot searches for ground
#define GAIT_STEADY_DEG   15.0f   // no lift-off above this tilt

// Legs, for the contact hook: 0 LF, 1 RF, 2 LR, 3 RR.
typedef bool (*gait_contact_fn)(uint8_t leg);

void gait_init(void);                  // call after muscle_init()
void gait_bind_contact(gait_contact_fn contact);   // optional foot sensing
bool gait_start(const char *name);     // "crawl", "trot", "pace", "bound"
void gait_stop(void);                  // stop and relax every muscle
void gait_set_speed(uint8_t pct);      // desire: 0 stands, 100 = GAIT_MAX_HZ
void gait_set_effort(uint8_t pct);     // activation for support/swing, default 70
void gait_show(void);

// CLI words
void gait_cli_crawl(void);
void gait_cli_trot(void);
void gait_cli_pace(void);
void gait_cli_bound(void);
void gait_cli_speed(void);     // ( pct -- )
void gait_cli_effort(void);    // ( pct -- )
void gait_cli_trace(void);     // toggle printing lift-off/touchdown events
void gait_cli_level(void);     // toggle IMU leveling

#endif
