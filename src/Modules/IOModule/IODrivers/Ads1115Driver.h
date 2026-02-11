#pragma once
/**
 * @file Ads1115Driver.h
 * @brief ADS1115 async driver wrapper.
 */

#include <ADS1X15.h>
#include <stdint.h>
#include "Modules/IOModule/IOBus/I2CBus.h"
#include "Modules/IOModule/IODrivers/IODriver.h"

struct Ads1115DriverConfig {
    uint8_t address = 0x48;
    uint8_t gain = ADS1X15_GAIN_6144MV;
    uint8_t dataRate = 1;
    uint32_t pollMs = 125;
    bool differentialPairs = false;
    float voltLsb = 0.0001875f;
};

class Ads1115Driver : public IODriver {
public:
    Ads1115Driver(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg);

    const char* id() const override { return driverId_; }
    bool begin() override;
    void tick(uint32_t nowMs) override;

    bool readRawChannel(uint8_t ch, int16_t& outRaw) const;
    bool readRawDifferential01(int16_t& outRaw) const;
    bool readRawDifferential23(int16_t& outRaw) const;

    bool readVoltsChannel(uint8_t ch, float& outV) const;
    bool readVoltsDifferential01(float& outV) const;
    bool readVoltsDifferential23(float& outV) const;
    bool readSampleSeqChannel(uint8_t ch, uint32_t& outSeq) const;
    bool readSampleSeqDifferential01(uint32_t& outSeq) const;
    bool readSampleSeqDifferential23(uint32_t& outSeq) const;

private:
    void requestNext_();
    float toVolts_(int16_t raw);

    const char* driverId_ = nullptr;
    I2CBus* bus_ = nullptr;
    Ads1115DriverConfig cfg_{};

    ADS1115 ads_;
    bool ready_ = false;
    uint32_t lastTickMs_ = 0;

    bool requested_ = false;
    uint8_t nextSingleCh_ = 0;
    uint8_t nextDiffPair_ = 0;

    bool validSingle_[4] = {false, false, false, false};
    int16_t rawSingle_[4] = {0, 0, 0, 0};
    float vSingle_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t seqSingle_[4] = {0, 0, 0, 0};
    bool validDiff01_ = false;
    bool validDiff23_ = false;
    int16_t rawDiff01_ = 0;
    int16_t rawDiff23_ = 0;
    float vDiff01_ = 0.0f;
    float vDiff23_ = 0.0f;
    uint32_t seqDiff01_ = 0;
    uint32_t seqDiff23_ = 0;
};
