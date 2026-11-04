#ifndef SENSOR_I2C_DRIVER_H
#define SENSOR_I2C_DRIVER_H
#include <stdint.h>
int MLX90640_I2CRead(uint8_t address, uint16_t reg, uint16_t count, uint16_t *data);
int MLX90640_I2CWrite(uint8_t address, uint16_t reg, uint16_t data);
#endif
