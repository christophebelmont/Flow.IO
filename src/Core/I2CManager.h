#pragma once
/**
 * @file I2CManager.h
 * @brief Thread-safe I2C access wrapper.
 */
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Manages shared I2C bus access with a mutex.
 */
class I2CManager {
public:
    /** @brief Initialize the I2C bus with SDA/SCL pins. */
    void begin(int sda, int scl);

    /** @brief Lock the I2C mutex (returns false on timeout). */
    bool lock(uint32_t timeoutMs);
    /** @brief Unlock the I2C mutex. */
    void unlock();

    /** @brief Probe a device address. */
    bool probe(uint8_t addr);

    /** @brief Write a register block to a device. */
    bool writeReg(uint8_t addr, uint8_t reg,
                  const uint8_t* data, uint16_t len);

    /** @brief Read a register block from a device. */
    bool readReg(uint8_t addr, uint8_t reg,
                 uint8_t* data, uint16_t len);

private:
    SemaphoreHandle_t mutex = nullptr;
};
