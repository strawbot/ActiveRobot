// Robot/io/mpu6050/mpu6050.h — MPU-6050 6-DOF accelerometer + gyroscope.
//
// Board-agnostic: talks to the chip only through the mpu6050_hw_t function
// pointers the board supplies via mpu6050_bind_hw(). No vendor headers, no
// pin maps — see Robot/CLAUDE.md.
//
// Unlike ADS1015, this chip free-runs its own sample rate once awake —
// reading the output registers is a plain synchronous burst read, no
// request/wait step needed.
//
// Register protocol per InvenSense MPU-6000/MPU-6050 Register Map and
// Descriptions (RM-MPU-6000A). Left at power-on-default full scale:
// accel +-2g, gyro +-250 deg/s — callers scale the raw counts themselves
// (32768 counts = 2g / 250 deg/s respectively) if they need physical units.

#ifndef ROBOT_IO_MPU6050_H
#define ROBOT_IO_MPU6050_H

#include <stdint.h>
#include <stdbool.h>

// Fixed 7-bit I2C address on this board (AD0 pin tied low).
#define MPU6050_ADDR 0x68U

// Board-supplied raw I2C transaction hooks. Each call is a full
// START..STOP transaction (see Board/i2c1.h on Nano for the reference
// implementation). addr7 is the chip's 7-bit I2C address.
typedef struct {
    bool (*i2c_write)(uint8_t addr7, const uint8_t *data, uint32_t len);
    bool (*i2c_read)(uint8_t addr7, uint8_t *data, uint32_t len);
} mpu6050_hw_t;

// mpu6050_bind_hw — board supplies its I2C transaction functions. Call
// once, before any other mpu6050_* call.
void mpu6050_bind_hw(const mpu6050_hw_t *hw);

typedef struct {
    int16_t accel_x, accel_y, accel_z;
    int16_t temp_raw;   // see mpu6050_temp_c()
    int16_t gyro_x, gyro_y, gyro_z;
} mpu6050_data_t;

// mpu6050_wake — clear the sleep bit in PWR_MGMT_1 (the chip powers on
// asleep). Call once before mpu6050_read_all().
bool mpu6050_wake(uint8_t addr7);

// mpu6050_read_all — burst-read ACCEL_XOUT_H..GYRO_ZOUT_L (14 bytes) in
// one transaction. Returns false on I2C failure.
bool mpu6050_read_all(uint8_t addr7, mpu6050_data_t *out);

// mpu6050_temp_c — datasheet formula for the on-chip die temperature.
static inline float mpu6050_temp_c(int16_t temp_raw) {
    return ((float)temp_raw) / 340.0f + 36.53f;
}

#endif // ROBOT_IO_MPU6050_H
