// Robot/io/mpu6050/mpu6050.c — see mpu6050.h for the API contract.

#include "mpu6050.h"
#include <stddef.h>

#define MPU6050_REG_PWR_MGMT_1  0x6BU
#define MPU6050_REG_ACCEL_XOUT_H 0x3BU
#define MPU6050_BURST_LEN 14U   // accel(6) + temp(2) + gyro(6)

static const mpu6050_hw_t *g_hw;

void mpu6050_bind_hw(const mpu6050_hw_t *hw) {
    g_hw = hw;
}

bool mpu6050_wake(uint8_t addr7) {
    if (g_hw == NULL) {
        return false;
    }
    uint8_t buf[2] = { MPU6050_REG_PWR_MGMT_1, 0x00U };   // clear SLEEP, default clock
    return g_hw->i2c_write(addr7, buf, sizeof(buf));
}

bool mpu6050_read_all(uint8_t addr7, mpu6050_data_t *out) {
    if (g_hw == NULL || out == NULL) {
        return false;
    }

    uint8_t ptr = MPU6050_REG_ACCEL_XOUT_H;
    if (!g_hw->i2c_write(addr7, &ptr, 1)) {
        return false;
    }

    uint8_t buf[MPU6050_BURST_LEN];
    if (!g_hw->i2c_read(addr7, buf, sizeof(buf))) {
        return false;
    }

    out->accel_x  = (int16_t)(((uint16_t)buf[0]  << 8) | buf[1]);
    out->accel_y  = (int16_t)(((uint16_t)buf[2]  << 8) | buf[3]);
    out->accel_z  = (int16_t)(((uint16_t)buf[4]  << 8) | buf[5]);
    out->temp_raw = (int16_t)(((uint16_t)buf[6]  << 8) | buf[7]);
    out->gyro_x   = (int16_t)(((uint16_t)buf[8]  << 8) | buf[9]);
    out->gyro_y   = (int16_t)(((uint16_t)buf[10] << 8) | buf[11]);
    out->gyro_z   = (int16_t)(((uint16_t)buf[12] << 8) | buf[13]);
    return true;
}
