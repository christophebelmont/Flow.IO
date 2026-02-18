#pragma once
/**
 * @file HmiDriverTypes.h
 * @brief Driver abstraction for HMI devices.
 */

#include <stdint.h>

#include "Modules/HMIModule/ConfigMenuModel.h"

enum class HmiEventType : uint8_t {
    None = 0,
    Home = 1,
    Back = 2,
    Validate = 3,
    NextPage = 4,
    PrevPage = 5,
    RowActivate = 6,
    RowToggle = 7,
    RowCycle = 8,
    RowSetText = 9,
    RowSetSlider = 10
};

struct HmiEvent {
    HmiEventType type = HmiEventType::None;
    uint8_t row = 0;
    int8_t direction = 1;
    float sliderValue = 0.0f;
    char text[48]{};
};

class IHmiDriver {
public:
    virtual ~IHmiDriver() = default;

    virtual const char* driverId() const = 0;
    virtual bool begin() = 0;
    virtual void tick(uint32_t nowMs) = 0;
    virtual bool pollEvent(HmiEvent& out) = 0;
    virtual bool renderConfigMenu(const ConfigMenuView& view) = 0;
};
