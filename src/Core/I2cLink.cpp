/**
 * @file I2cLink.cpp
 * @brief Core I2C helper implementation.
 */

#include "Core/I2cLink.h"
#define LOG_TAG "I2cLink"
#include "Core/ModuleLog.h"

I2cLink* I2cLink::slaveInstanceByBus_[2] = {nullptr, nullptr};

bool I2cLink::beginMaster(uint8_t bus, int sda, int scl, uint32_t freqHz)
{
    end();
    wire_ = selectWire_(bus);
    if (!wire_) return false;

    bus_ = (bus == 0) ? 0 : 1;
    if (!wire_->begin(sda, scl, freqHz)) {
        wire_ = nullptr;
        return false;
    }
    isSlave_ = false;
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    LOGI("I2C master started bus=%u sda=%d scl=%d freq=%lu",
         (unsigned)bus_, sda, scl, (unsigned long)freqHz);
    return true;
}

bool I2cLink::beginSlave(uint8_t bus, uint8_t address, int sda, int scl, uint32_t freqHz)
{
    end();
    wire_ = selectWire_(bus);
    if (!wire_) return false;

    bus_ = (bus == 0) ? 0 : 1;
    if (!wire_->begin((int)address, sda, scl, freqHz)) {
        wire_ = nullptr;
        return false;
    }

    isSlave_ = true;
    slaveInstanceByBus_[bus_] = this;
    if (bus_ == 0) {
        wire_->onReceive(onReceive0_);
        wire_->onRequest(onRequest0_);
    } else {
        wire_->onReceive(onReceive1_);
        wire_->onRequest(onRequest1_);
    }
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    LOGI("I2C slave started bus=%u addr=0x%02X sda=%d scl=%d freq=%lu",
         (unsigned)bus_, (unsigned)address, sda, scl, (unsigned long)freqHz);
    return true;
}

void I2cLink::end()
{
    if (isSlave_ && bus_ < 2 && slaveInstanceByBus_[bus_] == this) {
        slaveInstanceByBus_[bus_] = nullptr;
    }
    if (wire_) {
        wire_->end();
    }
    wire_ = nullptr;
    isSlave_ = false;
}

bool I2cLink::setSlaveCallbacks(I2cLinkReceiveCallback onReceive,
                                I2cLinkRequestCallback onRequest,
                                void* ctx)
{
    onReceiveCb_ = onReceive;
    onRequestCb_ = onRequest;
    cbCtx_ = ctx;
    return true;
}

bool I2cLink::lock(uint32_t timeoutMs)
{
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void I2cLink::unlock()
{
    if (!mutex_) return;
    xSemaphoreGive(mutex_);
}

bool I2cLink::transfer(uint8_t address,
                       const uint8_t* tx,
                       size_t txLen,
                       uint8_t* rx,
                       size_t rxMaxLen,
                       size_t& rxLenOut)
{
    rxLenOut = 0;
    if (!wire_ || isSlave_) return false;
    if (!tx || txLen == 0 || txLen > 255) return false;
    if (!lock(100)) return false;

    wire_->beginTransmission((int)address);
    const size_t wrote = wire_->write(tx, txLen);
    if (wrote != txLen) {
        unlock();
        return false;
    }
    const uint8_t err = wire_->endTransmission(true);
    if (err != 0) {
        unlock();
        return false;
    }

    if (!rx || rxMaxLen == 0) {
        unlock();
        return true;
    }

    // Give the slave callback time to build the response frame.
    // With cfg over I2C we observed deterministic one-frame lag on the first
    // read at lower delays, which forced client-side retries.
    constexpr uint32_t kSlaveProcessDelayUs = 4500U;
    delayMicroseconds(kSlaveProcessDelayUs);

    size_t got = wire_->requestFrom((int)address, (int)rxMaxLen, (int)true);
    if (got == 0) {
        // One lightweight retry to tolerate transient slave latency.
        delay(2);
        got = wire_->requestFrom((int)address, (int)rxMaxLen, (int)true);
    }
    if (got == 0) {
        unlock();
        return false;
    }

    size_t n = 0;
    while (wire_->available() && n < got) {
        rx[n++] = (uint8_t)wire_->read();
    }
    rxLenOut = n;
    unlock();
    return true;
}

void I2cLink::onReceive0_(int len)
{
    I2cLink* self = slaveInstanceByBus_[0];
    if (self) self->onReceive_(len);
}

void I2cLink::onReceive1_(int len)
{
    I2cLink* self = slaveInstanceByBus_[1];
    if (self) self->onReceive_(len);
}

void I2cLink::onRequest0_()
{
    I2cLink* self = slaveInstanceByBus_[0];
    if (self) self->onRequest_();
}

void I2cLink::onRequest1_()
{
    I2cLink* self = slaveInstanceByBus_[1];
    if (self) self->onRequest_();
}

void I2cLink::onReceive_(int len)
{
    if (!wire_ || !onReceiveCb_ || len <= 0) return;
    uint8_t buf[128];
    size_t n = 0;
    while (wire_->available() && n < sizeof(buf)) {
        buf[n++] = (uint8_t)wire_->read();
    }
    if (n == 0) return;
    onReceiveCb_(cbCtx_, buf, n);
}

void I2cLink::onRequest_()
{
    if (!wire_ || !onRequestCb_) return;
    uint8_t buf[128];
    const size_t n = onRequestCb_(cbCtx_, buf, sizeof(buf));
    if (n == 0) {
        const uint8_t zero = 0;
        wire_->write(&zero, 1);
        return;
    }
    wire_->write(buf, n);
}

TwoWire* I2cLink::selectWire_(uint8_t bus) const
{
    if (bus == 0) return &Wire;
#if defined(ESP32)
    return &Wire1;
#else
    return nullptr;
#endif
}
