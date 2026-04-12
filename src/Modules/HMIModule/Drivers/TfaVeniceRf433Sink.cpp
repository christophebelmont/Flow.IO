/**
 * @file TfaVeniceRf433Sink.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/Drivers/TfaVeniceRf433Sink.h"

#include <Arduino.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"

namespace {

static constexpr rmt_channel_t kRmtChannel = RMT_CHANNEL_0;
static constexpr uint8_t kModelId = 0x46U;
static constexpr uint8_t kHumidityPct = 50U;
static constexpr uint16_t kManchesterClockUs = 976U;
static constexpr uint8_t kPreamble0Bits = 12U;
static constexpr uint8_t kPreambleBits = 15U;
static constexpr uint8_t kSequenceBits = 50U;

}  // namespace

void TfaVeniceRf433Sink::setConfig(const TfaVeniceRf433Config& cfg)
{
    const bool changed =
        cfg_.enabled != cfg.enabled ||
        cfg_.txPin != cfg.txPin ||
        cfg_.periodMs != cfg.periodMs ||
        cfg_.channel != cfg.channel ||
        cfg_.sensorId != cfg.sensorId;
    if (!changed) return;

    cfg_ = cfg;
    configDirty_ = true;
    lastAttemptMs_ = 0U;
}

uint32_t TfaVeniceRf433Sink::sanitizePeriodMs_(uint32_t periodMs)
{
    return (periodMs < 5000U) ? 5000U : periodMs;
}

uint8_t TfaVeniceRf433Sink::sanitizeChannel_(uint8_t channel)
{
    if (channel < 1U) return 0U;
    if (channel > 8U) return 7U;
    return (uint8_t)(channel - 1U);
}

void TfaVeniceRf433Sink::shutdown_()
{
    if (started_) {
        (void)rmt_driver_uninstall(kRmtChannel);
    }
    started_ = false;
    if (txItems_) {
        heap_caps_free(txItems_);
        txItems_ = nullptr;
    }
}

bool TfaVeniceRf433Sink::ensureReady_()
{
    if (!cfg_.enabled || cfg_.txPin < 0) {
        shutdown_();
        configDirty_ = false;
        return false;
    }

    if (started_ && !configDirty_) return true;

    shutdown_();

    txItems_ = static_cast<rmt_item32_t*>(
        heap_caps_calloc(kFrameBitCount, sizeof(rmt_item32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    if (!txItems_) {
        LOGE("Venice RF433 buffer allocation failed");
        return false;
    }

    pinMode(cfg_.txPin, OUTPUT);
    digitalWrite(cfg_.txPin, LOW);

    rmt_config_t config = RMT_DEFAULT_CONFIG_TX((gpio_num_t)cfg_.txPin, kRmtChannel);
    config.clk_div = 80;
    config.tx_config.loop_en = false;
    config.tx_config.idle_output_en = true;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        LOGE("Venice RF433 rmt_config failed gpio=%d err=%d", (int)cfg_.txPin, (int)err);
        return false;
    }
    err = rmt_driver_install(config.channel, 0, 0);
    if (err != ESP_OK) {
        LOGE("Venice RF433 rmt_driver_install failed gpio=%d err=%d", (int)cfg_.txPin, (int)err);
        return false;
    }

    started_ = true;
    configDirty_ = false;
    return true;
}

bool TfaVeniceRf433Sink::readWaterTemp_(const IOServiceV2* ioSvc, IoId waterTempIoId, float& outTempC) const
{
    if (!ioSvc || !ioSvc->readAnalog) return false;
    if (waterTempIoId == IO_ID_INVALID) return false;
    return ioSvc->readAnalog(ioSvc->ctx, waterTempIoId, &outTempC, nullptr, nullptr) == IO_OK;
}

uint8_t TfaVeniceRf433Sink::lfsrDigest8_(const uint8_t* message, unsigned bytes, uint8_t gen, uint8_t key)
{
    uint8_t sum = 0U;
    if (!message) return sum;
    for (unsigned k = 0; k < bytes; ++k) {
        uint8_t data = message[k];
        for (int i = 7; i >= 0; --i) {
            if ((data >> i) & 1U) sum ^= key;
            key = (key & 1U) ? (uint8_t)((key >> 1) ^ gen) : (uint8_t)(key >> 1);
        }
    }
    return sum;
}

void TfaVeniceRf433Sink::fillPackedBits_(uint8_t* dst, size_t dstLen, const uint8_t* src, uint16_t bitPos, uint8_t bitLen)
{
    if (!dst || !src || dstLen == 0U || bitLen == 0U) return;

    uint16_t startByte = (uint16_t)(bitPos / 8U);
    const uint8_t startBit = (uint8_t)(bitPos % 8U);
    const uint8_t byteCount = (uint8_t)(bitLen / 8U + 1U);

    for (uint8_t i = 0; i < byteCount; ++i) {
        const uint16_t dstByte = (uint16_t)(startByte + i);
        if (dstByte < dstLen) {
            dst[dstByte] |= (uint8_t)(src[i] >> startBit);
        }
        if (startBit != 0U && (dstByte + 1U) < dstLen) {
            dst[dstByte + 1U] |= (uint8_t)(src[i] << (8U - startBit));
        }
    }
}

void TfaVeniceRf433Sink::encodeManchester_(const uint8_t* frame, uint16_t bitCount)
{
    if (!frame || !txItems_) return;
    for (uint16_t i = 0; i < bitCount && i < kFrameBitCount; ++i) {
        const uint8_t byte = frame[i / 8U];
        const uint8_t bit = (uint8_t)((byte >> (7U - (i % 8U))) & 1U);
        txItems_[i].duration0 = kManchesterClockUs / 2U;
        txItems_[i].duration1 = kManchesterClockUs / 2U;
        if (bit != 0U) {
            txItems_[i].level0 = 1U;
            txItems_[i].level1 = 0U;
        } else {
            txItems_[i].level0 = 0U;
            txItems_[i].level1 = 1U;
        }
    }
}

bool TfaVeniceRf433Sink::sendFrameForTemp_(float waterTempC)
{
    memset(frameBytes_, 0, sizeof(frameBytes_));

    uint8_t seq[kSeqByteCount]{};
    const uint8_t preamble0[2] = {0x00U, 0x10U};
    const uint8_t preamble[2] = {0x3FU, 0xFAU};

    int tempRaw = (int)(waterTempC * 18.0f) + 720;
    if (tempRaw < 0) tempRaw = 0;
    if (tempRaw > 0x0FFF) tempRaw = 0x0FFF;

    seq[0] = kModelId;
    seq[1] = cfg_.sensorId;
    seq[2] = (uint8_t)(sanitizeChannel_(cfg_.channel) << 4U);
    seq[2] |= (uint8_t)((tempRaw & 0x0F00) >> 8U);
    seq[3] = (uint8_t)(tempRaw & 0x00FF);
    seq[4] = kHumidityPct;
    seq[5] = (uint8_t)(lfsrDigest8_(seq, 5U, 0x98U, 0x3EU) ^ 0x64U);
    seq[6] = 0x00U;

    uint16_t bitPos = 0U;
    fillPackedBits_(frameBytes_, sizeof(frameBytes_), preamble0, bitPos, kPreamble0Bits);
    bitPos = (uint16_t)(bitPos + kPreamble0Bits);
    fillPackedBits_(frameBytes_, sizeof(frameBytes_), seq, bitPos, kSequenceBits);
    bitPos = (uint16_t)(bitPos + kSequenceBits);
    fillPackedBits_(frameBytes_, sizeof(frameBytes_), preamble, bitPos, kPreambleBits);
    bitPos = (uint16_t)(bitPos + kPreambleBits);
    fillPackedBits_(frameBytes_, sizeof(frameBytes_), seq, bitPos, kSequenceBits);
    bitPos = (uint16_t)(bitPos + kSequenceBits);
    fillPackedBits_(frameBytes_, sizeof(frameBytes_), preamble, bitPos, kPreambleBits);
    bitPos = (uint16_t)(bitPos + kPreambleBits);
    fillPackedBits_(frameBytes_, sizeof(frameBytes_), seq, bitPos, kSequenceBits);
    bitPos = (uint16_t)(bitPos + kSequenceBits);

    encodeManchester_(frameBytes_, bitPos);
    return txItems_ && rmt_write_items(kRmtChannel, txItems_, bitPos, true) == ESP_OK;
}

void TfaVeniceRf433Sink::tick(uint32_t nowMs, const IOServiceV2* ioSvc, IoId waterTempIoId)
{
    if (!cfg_.enabled) {
        shutdown_();
        lastAttemptMs_ = 0U;
        configDirty_ = false;
        return;
    }

    if (!ensureReady_()) return;
    if (lastAttemptMs_ != 0U &&
        (uint32_t)(nowMs - lastAttemptMs_) < sanitizePeriodMs_(cfg_.periodMs)) {
        return;
    }
    lastAttemptMs_ = nowMs;

    float waterTempC = 0.0f;
    if (!readWaterTemp_(ioSvc, waterTempIoId, waterTempC)) return;
    (void)sendFrameForTemp_(waterTempC);
}
