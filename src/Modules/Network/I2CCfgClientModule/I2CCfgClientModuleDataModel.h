#pragma once
/**
 * @file I2CCfgClientModuleDataModel.h
 * @brief Supervisor-side mirror of Flow runtime values.
 */

#include <stdint.h>

struct FlowRemoteRuntimeData {
    bool ready = false;
    bool linkOk = false;
    char firmware[24]{};
    bool hasRssi = false;
    int32_t rssiDbm = -127;
    bool hasHeapFrag = false;
    uint8_t heapFragPct = 0;
    bool mqttReady = false;
    uint32_t mqttRxDrop = 0;
    uint32_t mqttParseFail = 0;
    uint32_t i2cReqCount = 0;
    uint32_t i2cBadReqCount = 0;
    uint32_t i2cLastReqAgoMs = 0;
    bool hasPoolModes = false;
    bool filtrationAuto = false;
    bool winterMode = false;
    bool phAutoMode = false;
    bool orpAutoMode = false;
    bool filtrationOn = false;
    bool phPumpOn = false;
    bool chlorinePumpOn = false;
    bool hasPh = false;
    float phValue = 0.0f;
    bool hasOrp = false;
    float orpValue = 0.0f;
    bool hasWaterTemp = false;
    float waterTemp = 0.0f;
    bool hasAirTemp = false;
    float airTemp = 0.0f;
};

// MODULE_DATA_MODEL: FlowRemoteRuntimeData flowRemote
