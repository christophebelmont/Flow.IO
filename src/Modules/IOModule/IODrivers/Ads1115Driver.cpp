/**
 * @file Ads1115Driver.cpp
 * @brief Implementation file.
 */

#include "Ads1115Driver.h"

Ads1115Driver::Ads1115Driver(const char* driverId, I2CBus* bus, const Ads1115DriverConfig& cfg)
    : driverId_(driverId), bus_(bus), cfg_(cfg), ads_(cfg.address, bus ? bus->wire() : &Wire)
{
}

bool Ads1115Driver::begin()
{
    if (!bus_) return false;
    if (!bus_->lock(50)) return false;

    bool ok = ads_.begin() && ads_.isConnected();
    if (ok) {
        ads_.setGain(cfg_.gain);
        ads_.setDataRate(cfg_.dataRate);
        ready_ = true;
        requestNext_();
    }

    bus_->unlock();
    return ok;
}

void Ads1115Driver::tick(uint32_t nowMs)
{
    if (!ready_ || !bus_) return;
    if ((uint32_t)(nowMs - lastTickMs_) < cfg_.pollMs) return;
    lastTickMs_ = nowMs;

    if (!bus_->lock(20)) return;

    if (requested_ && ads_.isReady()) {
        int16_t raw = ads_.getValue();
        float v = toVolts_(raw);

        if (cfg_.differentialPairs) {
            if (nextDiffPair_ == 0) {
                validDiff23_ = true;
                rawDiff23_ = raw;
                vDiff23_ = v;
                ++seqDiff23_;
            } else {
                validDiff01_ = true;
                rawDiff01_ = raw;
                vDiff01_ = v;
                ++seqDiff01_;
            }
        } else {
            uint8_t prevCh = (uint8_t)((nextSingleCh_ + 3) % 4);
            validSingle_[prevCh] = true;
            rawSingle_[prevCh] = raw;
            vSingle_[prevCh] = v;
            ++seqSingle_[prevCh];
        }

        requestNext_();
    }

    bus_->unlock();
}

bool Ads1115Driver::readVoltsChannel(uint8_t ch, float& outV) const
{
    if (ch > 3 || !validSingle_[ch]) return false;
    outV = vSingle_[ch];
    return true;
}

bool Ads1115Driver::readRawChannel(uint8_t ch, int16_t& outRaw) const
{
    if (ch > 3 || !validSingle_[ch]) return false;
    outRaw = rawSingle_[ch];
    return true;
}

bool Ads1115Driver::readVoltsDifferential01(float& outV) const
{
    if (!validDiff01_) return false;
    outV = vDiff01_;
    return true;
}

bool Ads1115Driver::readRawDifferential01(int16_t& outRaw) const
{
    if (!validDiff01_) return false;
    outRaw = rawDiff01_;
    return true;
}

bool Ads1115Driver::readVoltsDifferential23(float& outV) const
{
    if (!validDiff23_) return false;
    outV = vDiff23_;
    return true;
}

bool Ads1115Driver::readRawDifferential23(int16_t& outRaw) const
{
    if (!validDiff23_) return false;
    outRaw = rawDiff23_;
    return true;
}

bool Ads1115Driver::readSampleSeqChannel(uint8_t ch, uint32_t& outSeq) const
{
    if (ch > 3 || !validSingle_[ch]) return false;
    outSeq = seqSingle_[ch];
    return true;
}

bool Ads1115Driver::readSampleSeqDifferential01(uint32_t& outSeq) const
{
    if (!validDiff01_) return false;
    outSeq = seqDiff01_;
    return true;
}

bool Ads1115Driver::readSampleSeqDifferential23(uint32_t& outSeq) const
{
    if (!validDiff23_) return false;
    outSeq = seqDiff23_;
    return true;
}

void Ads1115Driver::requestNext_()
{
    if (cfg_.differentialPairs) {
        if (nextDiffPair_ == 0) {
            ads_.requestADC_Differential_0_1();
            nextDiffPair_ = 1;
        } else {
            ads_.requestADC_Differential_2_3();
            nextDiffPair_ = 0;
        }
    } else {
        ads_.requestADC(nextSingleCh_);
        nextSingleCh_ = (uint8_t)((nextSingleCh_ + 1) % 4);
    }

    requested_ = true;
}

float Ads1115Driver::toVolts_(int16_t raw)
{
    float v = ads_.toVoltage(raw);
    if (v <= ADS1X15_INVALID_VOLTAGE) {
        return (float)raw * cfg_.voltLsb;
    }
    return v;
}
