/**
 * @file I2CManager.cpp
 * @brief Implementation file.
 */
#include "I2CManager.h"
#define LOG_TAG_CORE "I2cManag"

void I2CManager::begin(int sda, int scl) {
    Wire.begin(sda, scl);
    if (!mutex) mutex = xSemaphoreCreateMutex();
}

bool I2CManager::lock(uint32_t timeoutMs) {
    return xSemaphoreTake(mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void I2CManager::unlock() {
    xSemaphoreGive(mutex);
}

bool I2CManager::probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

bool I2CManager::writeReg(uint8_t addr, uint8_t reg,
                          const uint8_t* data, uint16_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    for (uint16_t i = 0; i < len; ++i) Wire.write(data[i]);
    return Wire.endTransmission() == 0;
}

bool I2CManager::readReg(uint8_t addr, uint8_t reg,
                         uint8_t* data, uint16_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;

    uint16_t read = Wire.requestFrom(addr, (uint8_t)len);
    if (read != len) return false;
    for (uint16_t i = 0; i < len; ++i) data[i] = Wire.read();
    return true;
}
