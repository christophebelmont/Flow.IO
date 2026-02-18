#pragma once
/**
 * @file ConfigMenuModel.h
 * @brief UI-agnostic configuration menu model with pagination and typed fields.
 */

#include <stddef.h>
#include <stdint.h>

#include "Core/Services/IConfig.h"

enum class ConfigMenuWidget : uint8_t {
    Text = 0,
    Switch = 1,
    Select = 2,
    Slider = 3
};

enum class ConfigMenuValueType : uint8_t {
    Unknown = 0,
    Bool = 1,
    Int = 2,
    Float = 3,
    Text = 4
};

struct ConfigMenuConstraints {
    ConfigMenuWidget widget = ConfigMenuWidget::Text;
    bool editable = true;
    float minValue = 0.0f;
    float maxValue = 100.0f;
    float step = 1.0f;
    const char* optionsCsv = nullptr; // "auto|manual|winter"
};

struct ConfigMenuHint {
    const char* module = nullptr; // exact module name (ex: "poollogic"), nullptr for wildcard
    const char* key = nullptr;    // exact key name
    ConfigMenuConstraints constraints{};
};

struct ConfigMenuRowView {
    bool visible = false;
    bool editable = false;
    bool dirty = false;
    ConfigMenuWidget widget = ConfigMenuWidget::Text;
    char key[28]{};
    char label[28]{};
    char value[40]{};
};

struct ConfigMenuView {
    char breadcrumb[96]{};
    uint8_t pageIndex = 0;
    uint8_t pageCount = 1;
    uint8_t rowCountOnPage = 0;
    ConfigMenuRowView rows[6]{};

    bool canHome = true;
    bool canBack = false;
    bool canValidate = false;
    bool isHome = true;
};

class ConfigMenuModel {
public:
    static constexpr uint8_t RowsPerPage = 6;
    static constexpr uint8_t MaxRows = 72;
    static constexpr uint8_t MaxModules = 48;

    bool begin(const ConfigStoreService* cfgSvc);
    void setHints(const ConfigMenuHint* hints, uint8_t count);

    bool home();
    bool back();
    bool openModule(const char* module);
    bool refreshCurrent();
    bool enterRow(uint8_t rowOnPage);

    bool nextPage();
    bool prevPage();

    bool setText(uint8_t rowOnPage, const char* value);
    bool toggleSwitch(uint8_t rowOnPage);
    bool cycleSelect(uint8_t rowOnPage, int8_t direction = 1);
    bool setSlider(uint8_t rowOnPage, float value);

    bool validate(char* ack, size_t ackLen);
    void buildView(ConfigMenuView& out) const;

    bool isHome() const { return currentModule_[0] == '\0'; }
    const char* currentModule() const { return currentModule_; }
    uint8_t pageIndex() const { return pageIndex_; }
    uint8_t pageCount() const;

private:
    enum class RowKind : uint8_t {
        Module = 0,
        Config = 1
    };

    struct Row {
        RowKind kind = RowKind::Config;
        bool editable = false;
        bool dirty = false;
        ConfigMenuWidget widget = ConfigMenuWidget::Text;
        ConfigMenuValueType type = ConfigMenuValueType::Unknown;

        char module[28]{};
        char key[28]{};
        char label[28]{};
        char value[40]{};

        bool boolCur = false;
        bool boolOrig = false;

        int32_t intCur = 0;
        int32_t intOrig = 0;

        float floatCur = 0.0f;
        float floatOrig = 0.0f;

        char textCur[40]{};
        char textOrig[40]{};

        float sliderMin = 0.0f;
        float sliderMax = 100.0f;
        float sliderStep = 1.0f;

        uint8_t optionCount = 0;
        char options[6][16]{};
    };

    const ConfigStoreService* cfgSvc_ = nullptr;
    const ConfigMenuHint* hints_ = nullptr;
    uint8_t hintCount_ = 0;

    Row* rows_ = nullptr;
    uint8_t rowCount_ = 0;
    uint8_t pageIndex_ = 0;

    char (*moduleList_)[28] = nullptr;
    uint8_t moduleCount_ = 0;

    char currentModule_[28]{};
    char previousModule_[28]{};

    bool loadHome_();
    bool loadModule_(const char* module);
    bool resolvePageRow_(uint8_t rowOnPage, uint8_t& absoluteIdx) const;
    bool recomputeDirty_(Row& row);
    void formatValueText_(Row& row);
    void applyHints_(Row& row);
    const ConfigMenuHint* findHint_(const char* module, const char* key) const;
    bool parseOptions_(Row& row, const char* csv);
    void buildBreadcrumb_(char* out, size_t outLen) const;
    bool appendText_(char* out, size_t outLen, size_t& pos, const char* text) const;
};
