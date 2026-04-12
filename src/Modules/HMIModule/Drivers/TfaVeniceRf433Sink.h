#pragma once
/**
 * @file TfaVeniceRf433Sink.h
 * @brief RF433 telemetry sink for TFA Venice compatible remote displays.
 */

#include <stddef.h>
#include <stdint.h>

#include <driver/rmt.h>

#include "Core/Services/IIO.h"

struct TfaVeniceRf433Config {
    bool enabled = false;
    int8_t txPin = 14;
    uint32_t periodMs = 53000U;
    uint8_t channel = 1U;   // User-facing channel number [1..8]
    uint8_t sensorId = 0x48U;
};

class TfaVeniceRf433Sink {
public:
    void setConfig(const TfaVeniceRf433Config& cfg);
    void tick(uint32_t nowMs, const IOServiceV2* ioSvc, IoId waterTempIoId);

private:
    static constexpr size_t kFrameByteCount = 30U;
    static constexpr size_t kSeqByteCount = 7U;
    static constexpr size_t kFrameBitCount = 192U;

    bool ensureReady_();
    void shutdown_();
    bool readWaterTemp_(const IOServiceV2* ioSvc, IoId waterTempIoId, float& outTempC) const;
    bool sendFrameForTemp_(float waterTempC);
    void encodeManchester_(const uint8_t* frame, uint16_t bitCount);
    static uint8_t lfsrDigest8_(const uint8_t* message, unsigned bytes, uint8_t gen, uint8_t key);
    static void fillPackedBits_(uint8_t* dst, size_t dstLen, const uint8_t* src, uint16_t bitPos, uint8_t bitLen);
    static uint32_t sanitizePeriodMs_(uint32_t periodMs);
    static uint8_t sanitizeChannel_(uint8_t channel);

    TfaVeniceRf433Config cfg_{};
    bool configDirty_ = true;
    bool started_ = false;
    uint32_t lastAttemptMs_ = 0U;
    uint8_t frameBytes_[kFrameByteCount]{};
    rmt_item32_t* txItems_ = nullptr;
};
