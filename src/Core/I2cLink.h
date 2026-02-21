#pragma once
/**
 * @file I2cLink.h
 * @brief Core I2C helper supporting master and slave roles.
 */

#include <Arduino.h>
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef void (*I2cLinkReceiveCallback)(void* ctx, const uint8_t* data, size_t len);
typedef size_t (*I2cLinkRequestCallback)(void* ctx, uint8_t* out, size_t maxLen);

class I2cLink {
public:
    I2cLink() = default;

    bool beginMaster(uint8_t bus, int sda, int scl, uint32_t freqHz);
    bool beginSlave(uint8_t bus, uint8_t address, int sda, int scl, uint32_t freqHz);
    void end();

    bool setSlaveCallbacks(I2cLinkReceiveCallback onReceive,
                           I2cLinkRequestCallback onRequest,
                           void* ctx);

    bool transfer(uint8_t address,
                  const uint8_t* tx,
                  size_t txLen,
                  uint8_t* rx,
                  size_t rxMaxLen,
                  size_t& rxLenOut);

    bool lock(uint32_t timeoutMs);
    void unlock();

private:
    static void onReceive0_(int len);
    static void onReceive1_(int len);
    static void onRequest0_();
    static void onRequest1_();

    void onReceive_(int len);
    void onRequest_();

    TwoWire* selectWire_(uint8_t bus) const;

    TwoWire* wire_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    uint8_t bus_ = 0;
    bool isSlave_ = false;

    I2cLinkReceiveCallback onReceiveCb_ = nullptr;
    I2cLinkRequestCallback onRequestCb_ = nullptr;
    void* cbCtx_ = nullptr;

    static I2cLink* slaveInstanceByBus_[2];
};

