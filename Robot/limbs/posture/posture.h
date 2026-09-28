// Robot/limbs/posture/posture.h — body attitude from a 6-DOF IMU.
//
// Samples the IMU every POSTURE_PERIOD_MS through a board shim and keeps a
// roll/pitch estimate: gyro rates integrated for fast response, pulled
// toward the accelerometer's gravity direction so it never drifts
// (complementary filter). Observers are told when the body tips past
// POSTURE_TIP_DEG or turns over.
//
// Body frame (the board shim rotates its chip's axes into this):
//   x forward, y left, z up. Level and still reads accel (0, 0, +1000) mg.
//   roll  > 0  left side up / right side down   (rotation about x)
//   pitch > 0  nose down                        (rotation about y)

#ifndef ROBOT_POSTURE_H
#define ROBOT_POSTURE_H

#include <stdbool.h>
#include <stdint.h>

#define POSTURE_PERIOD_MS   20
#define POSTURE_TIP_DEG     45.0f

typedef struct {
    int16_t ax, ay, az;   // mg
    int16_t gx, gy, gz;   // 0.1 deg/s
} posture_imu_t;

// Board hardware shim: read one sample in body-frame units. Return false
// if the read failed; the sample is skipped.
typedef struct {
    bool (*read)(posture_imu_t *out);
} posture_hw_t;

void posture_init(void);
void posture_bind_hw(const posture_hw_t *ops);   // starts sampling
void posture_on_tip(void (*cb)(void));           // fixed capacity, at startup

bool  posture_valid(void);   // false until the first good sample
bool  posture_tipped(void);
float posture_roll(void);    // degrees
float posture_pitch(void);   // degrees

void posture_show(void);

#endif
