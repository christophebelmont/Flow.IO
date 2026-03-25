#pragma once

#include <stdint.h>

enum class IoCapability : uint8_t {
    None = 0,
    DigitalIn = 1,
    DigitalOut = 2,
    AnalogIn = 3,
    OneWireTemp = 4
};

enum class BoardSignal : uint8_t {
    None = 0,
    Relay1,
    Relay2,
    Relay3,
    Relay4,
    Relay5,
    Relay6,
    Relay7,
    Relay8,
    DigitalIn1,
    DigitalIn2,
    DigitalIn3,
    DigitalIn4,
    AnalogIn1,
    AnalogIn2,
    AnalogIn3,
    AnalogIn4,
    TempProbe1,
    TempProbe2
};

struct UartSpec {
    const char* name;
    uint8_t uartIndex;
    int8_t rxPin;
    int8_t txPin;
    uint32_t baud;
    bool primary;
};

struct I2cBusSpec {
    const char* name;
    uint8_t sdaPin;
    uint8_t sclPin;
    uint32_t frequencyHz;
};

struct OneWireBusSpec {
    const char* name;
    BoardSignal signal;
    uint8_t pin;
};

struct IoPointSpec {
    const char* name;
    IoCapability capability;
    BoardSignal signal;
    uint8_t pin;
    bool momentary;
    uint16_t pulseMs;
};

struct St7789DisplaySpec {
    uint16_t resX;
    uint16_t resY;
    uint8_t rotation;
    int8_t colStart;
    int8_t rowStart;
    int8_t backlightPin;
    int8_t csPin;
    int8_t dcPin;
    int8_t rstPin;
    int8_t mosiPin;
    int8_t sclkPin;
    bool swapColorBytes;
    bool invertColors;
    uint32_t spiHz;
    uint16_t minRenderGapMs;
};

struct SupervisorInputSpec {
    int8_t pirPin;
    uint16_t pirDebounceMs;
    bool pirActiveHigh;
    int8_t wifiResetPin;
    uint16_t wifiResetDebounceMs;
};

struct SupervisorUpdateSpec {
    int8_t flowIoEnablePin;
    int8_t flowIoBootPin;
    int8_t nextionRebootPin;
    uint32_t nextionUploadBaud;
};

struct SupervisorBoardSpec {
    St7789DisplaySpec display;
    SupervisorInputSpec inputs;
    SupervisorUpdateSpec update;
};

struct BoardSpec {
    const char* name;
    const UartSpec* uarts;
    uint8_t uartCount;
    const I2cBusSpec* i2cBuses;
    uint8_t i2cCount;
    const OneWireBusSpec* oneWireBuses;
    uint8_t oneWireCount;
    const IoPointSpec* ioPoints;
    uint8_t ioPointCount;
    const SupervisorBoardSpec* supervisor;
};
