#ifndef __SHT3X_H
#define __SHT3X_H

#include "stm32f1xx_hal.h"

typedef enum
{
    SHT3X_STATUS_OK = 0,
    SHT3X_STATUS_PARAM_ERROR,
    SHT3X_STATUS_I2C_ERROR,
    SHT3X_STATUS_CRC_ERROR
} SHT3x_StatusTypeDef;

#define SHT3X_ADDR    (0x44U << 1)

SHT3x_StatusTypeDef SHT3x_ReadTemperatureHumidity(
    I2C_HandleTypeDef *hi2c,
    float *temperature,
    float *humidity
);

#endif
