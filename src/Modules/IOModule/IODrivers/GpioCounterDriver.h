#pragma once
/**
 * @file GpioCounterDriver.h
 * @brief ESP32 GPIO pulse counter driver using GPIO interrupts.
 */

#include <Arduino.h>
#include <stdint.h>

#include "Modules/IOModule/IODrivers/IODriver.h"

class GpioCounterDriver : public IDigitalCounterDriver {
public:
    GpioCounterDriver(const char* driverId,
                      uint8_t pin,
                      bool activeHigh,
                      uint8_t inputPullMode,
                      uint32_t counterDebounceUs);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t) override {}

    bool write(bool) override { return false; }
    bool read(bool& on) const override;
    bool readCount(int32_t& count) const override;

private:
    static void IRAM_ATTR handleInterruptThunk_(void* arg);
    void IRAM_ATTR handleInterrupt_();

    const char* driverId_ = nullptr;
    uint8_t pin_ = 0;
    bool activeHigh_ = true;
    uint8_t inputPullMode_ = 0;
    uint32_t counterDebounceUs_ = 0;
    volatile int32_t pulseCount_ = 0;
    volatile bool lastLogicalState_ = false;
    volatile uint32_t lastPulseUs_ = 0;
    volatile uint32_t lastInactiveUs_ = 0;
};
