// Robot/limbs/posture/posture.c — see posture.h.

#include <math.h>
#include "posture.h"
#include "tea.h"
#include "printers.h"

#define ALPHA        0.98f   // gyro share per sample; accel corrects the rest
#define TIP_CLEAR    0.8f    // re-arm once back under 80 % of the tip angle
#define DEG(rad)     ((rad) * 57.29578f)
#define MAX_OBSERVERS 2

static const posture_hw_t *hw;
static void (*observers[MAX_OBSERVERS])(void);
static float roll, pitch;
static bool  valid, tipped;

static void notify_tip(void) {
    for (uint8_t i = 0; i < MAX_OBSERVERS; i++)
        if (observers[i]) observers[i]();
}

static void posture_sample(void) {
    posture_imu_t s;
    if (hw && hw->read(&s)) {
        float ax = s.ax, ay = s.ay, az = s.az;
        float acc_roll  = DEG(atan2f(ay, az));
        float acc_pitch = DEG(atan2f(-ax, sqrtf(ay * ay + az * az)));
        if (!valid) {
            roll  = acc_roll;
            pitch = acc_pitch;
            valid = true;
        } else {
            float dt = POSTURE_PERIOD_MS / 1000.0f;
            roll  = ALPHA * (roll  + s.gx * 0.1f * dt) + (1.0f - ALPHA) * acc_roll;
            pitch = ALPHA * (pitch + s.gy * 0.1f * dt) + (1.0f - ALPHA) * acc_pitch;
        }
        float worst = fmaxf(fabsf(roll), fabsf(pitch));
        if (!tipped && (worst > POSTURE_TIP_DEG || az < 0)) {
            tipped = true;
            notify_tip();
        } else if (tipped && worst < TIP_CLEAR * POSTURE_TIP_DEG && az > 0) {
            tipped = false;
        }
    }
    after(msec(POSTURE_PERIOD_MS), posture_sample);
}

void posture_init(void) {
    namedAction(posture_sample);
}

void posture_bind_hw(const posture_hw_t *ops) {
    hw = ops;
    valid = tipped = false;
    stop(posture_sample);
    later(posture_sample);
}

void posture_on_tip(void (*cb)(void)) {
    for (uint8_t i = 0; i < MAX_OBSERVERS; i++)
        if (!observers[i]) { observers[i] = cb; return; }
}

bool  posture_valid(void)  { return valid; }
bool  posture_tipped(void) { return tipped; }
float posture_roll(void)   { return roll; }
float posture_pitch(void)  { return pitch; }

void posture_show(void) {
    if (!valid) { print(hw ? "\nposture: no sample yet" : "\nposture: no IMU bound"); printCr(); return; }
    print("\nroll ");   printFloat0(roll, 1);
    print("  pitch ");  printFloat0(pitch, 1);
    if (tipped) print("  TIPPED");
    printCr();
}
