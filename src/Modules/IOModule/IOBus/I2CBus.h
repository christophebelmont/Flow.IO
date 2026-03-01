#pragma once
/**
 * @file I2CBus.h
 * @brief Shared I2C bus with mutex.
 */

#include <stdint.h>
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class I2CBus {
public:
    // IOModule is fixed on I2C bus 0 (global Wire). Only SDA/SCL/frequency are configurable.
    void begin(int sda, int scl, uint32_t frequencyHz = 100000);

    bool lock(uint32_t timeoutMs);
    void unlock();

    bool probe(uint8_t addr);

    bool writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t len);
    bool readReg(uint8_t addr, uint8_t reg, uint8_t* data, uint16_t len);

    bool writeBytes(uint8_t addr, const uint8_t* data, uint16_t len);
    bool readBytes(uint8_t addr, uint8_t* data, uint16_t len);

    TwoWire* wire() { return &Wire; }

private:
    SemaphoreHandle_t mutex_ = nullptr;
};
