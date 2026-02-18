#pragma once
/**
 * @file NextionDriver.h
 * @brief Nextion HMI driver implementation.
 */

#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"

#include <Arduino.h>

struct NextionDriverConfig {
    HardwareSerial* serial = &Serial2;
    int8_t rxPin = 16;
    int8_t txPin = 17;
    uint32_t baud = 115200;
    uint32_t minRenderGapMs = 120;
};

class NextionDriver final : public IHmiDriver {
public:
    NextionDriver() = default;

    void setConfig(const NextionDriverConfig& cfg) { cfg_ = cfg; }

    const char* driverId() const override { return "nextion"; }
    bool begin() override;
    void tick(uint32_t nowMs) override;
    bool pollEvent(HmiEvent& out) override;
    bool renderConfigMenu(const ConfigMenuView& view) override;

private:
    static constexpr uint8_t RxBufSize = 128;

    NextionDriverConfig cfg_{};
    bool started_ = false;
    bool pageReady_ = false;
    uint32_t lastRenderMs_ = 0;

    uint8_t rxBuf_[RxBufSize]{};
    uint8_t rxLen_ = 0;
    uint8_t ffCount_ = 0;

    bool parseFrame_(const uint8_t* frame, uint8_t len, HmiEvent& out);
    bool parseAsciiEvent_(const char* text, HmiEvent& out);
    bool parseTouchEvent_(const uint8_t* frame, uint8_t len, HmiEvent& out);

    bool sendCmd_(const char* cmd);
    bool sendCmdFmt_(const char* fmt, ...);
    bool sendText_(const char* objectName, const char* value);
    void sanitizeText_(char* out, size_t outLen, const char* in) const;
};
