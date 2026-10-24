#include "SHT3x.h"

#define SHT3X_TIMEOUT       100U
#define SHT3X_CONVERT_DELAY 20U

/* SHT3x CRC-8：多项式 0x31，初始值 0xFF */
static uint8_t SHT3x_CRC8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFFU;

    for (uint8_t i = 0; i < len; i++)
    {
        crc ^= data[i];

        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x80U)
            {
                crc = (uint8_t)((crc << 1) ^ 0x31U);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }

    return crc;
}

SHT3x_StatusTypeDef SHT3x_ReadTemperatureHumidity(
    I2C_HandleTypeDef *hi2c,
    float *temperature,
    float *humidity)
{
    uint8_t cmd[2] = {0x24, 0x00};
    uint8_t rx[6] = {0};

    uint16_t rawT;
    uint16_t rawRH;

    if ((hi2c == NULL) ||
        (temperature == NULL) ||
        (humidity == NULL))
    {
        return SHT3X_STATUS_PARAM_ERROR;
    }

    /* 1. 发送高重复性、非时钟拉伸单次测量命令 */
    if (HAL_I2C_Master_Transmit(
            hi2c,
            SHT3X_ADDR,
            cmd,
            2,
            SHT3X_TIMEOUT) != HAL_OK)
    {
        return SHT3X_STATUS_I2C_ERROR;
    }

    /* 2. 等待测量完成 */
    HAL_Delay(SHT3X_CONVERT_DELAY);

    /* 3. 读取温度、湿度及各自的 CRC */
    if (HAL_I2C_Master_Receive(
            hi2c,
            SHT3X_ADDR,
            rx,
            6,
            SHT3X_TIMEOUT) != HAL_OK)
    {
        return SHT3X_STATUS_I2C_ERROR;
    }

    /* 4. 分别检查温度和湿度 CRC */
    if (SHT3x_CRC8(&rx[0], 2) != rx[2])
    {
        return SHT3X_STATUS_CRC_ERROR;
    }

    if (SHT3x_CRC8(&rx[3], 2) != rx[5])
    {
        return SHT3X_STATUS_CRC_ERROR;
    }

    /* 5. 合成 16 位原始数据，高字节在前 */
    rawT = ((uint16_t)rx[0] << 8) | rx[1];
    rawRH = ((uint16_t)rx[3] << 8) | rx[4];

    /* 6. 换算成实际温湿度 */
    *temperature =
        -45.0f + 175.0f * (float)rawT / 65535.0f;

    *humidity =
        100.0f * (float)rawRH / 65535.0f;

    return SHT3X_STATUS_OK;
}
