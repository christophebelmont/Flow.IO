/**
 * @file ConfigMenuModel.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/ConfigMenuModel.h"

#include "Core/SystemLimits.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

static void copyStr_(char* dst, size_t cap, const char* src)
{
    if (!dst || cap == 0) return;
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}

static bool parseBoolText_(const char* text, bool& out)
{
    if (!text) return false;
    if (strcasecmp(text, "1") == 0 || strcasecmp(text, "true") == 0 ||
        strcasecmp(text, "on") == 0 || strcasecmp(text, "yes") == 0) {
        out = true;
        return true;
    }
    if (strcasecmp(text, "0") == 0 || strcasecmp(text, "false") == 0 ||
        strcasecmp(text, "off") == 0 || strcasecmp(text, "no") == 0) {
        out = false;
        return true;
    }
    return false;
}

static void trimFloat_(char* s)
{
    if (!s) return;
    char* dot = strchr(s, '.');
    if (!dot) return;

    char* end = s + strlen(s) - 1;
    while (end > dot && *end == '0') {
        *end = '\0';
        --end;
    }
    if (end == dot) *end = '\0';
}

static bool sameFloat_(float a, float b)
{
    return fabsf(a - b) <= 0.0001f;
}

} // namespace

bool ConfigMenuModel::begin(const ConfigStoreService* cfgSvc)
{
    if (!rows_) {
        rows_ = (Row*)calloc(MaxRows, sizeof(Row));
    }
    if (!moduleList_) {
        moduleList_ = (char(*)[28])calloc(MaxModules, sizeof(*moduleList_));
    }
    if (!rows_ || !moduleList_) return false;

    cfgSvc_ = cfgSvc;
    hints_ = nullptr;
    hintCount_ = 0;
    rowCount_ = 0;
    pageIndex_ = 0;
    moduleCount_ = 0;
    currentModule_[0] = '\0';
    previousModule_[0] = '\0';
    if (!cfgSvc_) return false;
    return loadHome_();
}

void ConfigMenuModel::setHints(const ConfigMenuHint* hints, uint8_t count)
{
    hints_ = hints;
    hintCount_ = count;
}

bool ConfigMenuModel::home()
{
    if (!isHome()) copyStr_(previousModule_, sizeof(previousModule_), currentModule_);
    return loadHome_();
}

bool ConfigMenuModel::back()
{
    if (isHome()) return false;
    if (previousModule_[0] != '\0') {
        char target[sizeof(previousModule_)]{};
        copyStr_(target, sizeof(target), previousModule_);
        previousModule_[0] = '\0';
        return loadModule_(target);
    }
    return loadHome_();
}

bool ConfigMenuModel::openModule(const char* module)
{
    if (!module || module[0] == '\0') return false;
    if (!isHome()) copyStr_(previousModule_, sizeof(previousModule_), currentModule_);
    else previousModule_[0] = '\0';
    return loadModule_(module);
}

bool ConfigMenuModel::refreshCurrent()
{
    if (isHome()) return loadHome_();

    char current[sizeof(currentModule_)]{};
    copyStr_(current, sizeof(current), currentModule_);
    return loadModule_(current);
}

bool ConfigMenuModel::enterRow(uint8_t rowOnPage)
{
    uint8_t idx = 0;
    if (!resolvePageRow_(rowOnPage, idx)) return false;
    const Row& r = rows_[idx];
    if (r.kind != RowKind::Module) return false;
    return openModule(r.module);
}

uint8_t ConfigMenuModel::pageCount() const
{
    if (rowCount_ == 0) return 1;
    return (uint8_t)((rowCount_ + RowsPerPage - 1U) / RowsPerPage);
}

bool ConfigMenuModel::nextPage()
{
    const uint8_t cnt = pageCount();
    if (pageIndex_ + 1U >= cnt) return false;
    ++pageIndex_;
    return true;
}

bool ConfigMenuModel::prevPage()
{
    if (pageIndex_ == 0) return false;
    --pageIndex_;
    return true;
}

bool ConfigMenuModel::resolvePageRow_(uint8_t rowOnPage, uint8_t& absoluteIdx) const
{
    if (rowOnPage >= RowsPerPage) return false;
    const uint16_t idx = (uint16_t)pageIndex_ * RowsPerPage + rowOnPage;
    if (idx >= rowCount_) return false;
    absoluteIdx = (uint8_t)idx;
    return true;
}

bool ConfigMenuModel::setText(uint8_t rowOnPage, const char* value)
{
    uint8_t idx = 0;
    if (!resolvePageRow_(rowOnPage, idx)) return false;
    Row& r = rows_[idx];
    if (r.kind != RowKind::Config || !r.editable) return false;
    if (!value) value = "";

    switch (r.type) {
        case ConfigMenuValueType::Bool: {
            bool b = false;
            if (!parseBoolText_(value, b)) return false;
            r.boolCur = b;
            break;
        }
        case ConfigMenuValueType::Int: {
            char* end = nullptr;
            const long v = strtol(value, &end, 10);
            if (!end || *end != '\0') return false;
            r.intCur = (int32_t)v;
            break;
        }
        case ConfigMenuValueType::Float: {
            char* end = nullptr;
            const float v = strtof(value, &end);
            if (!end || *end != '\0') return false;
            r.floatCur = v;
            break;
        }
        case ConfigMenuValueType::Text:
            copyStr_(r.textCur, sizeof(r.textCur), value);
            break;
        default:
            return false;
    }

    recomputeDirty_(r);
    formatValueText_(r);
    return true;
}

bool ConfigMenuModel::toggleSwitch(uint8_t rowOnPage)
{
    uint8_t idx = 0;
    if (!resolvePageRow_(rowOnPage, idx)) return false;
    Row& r = rows_[idx];
    if (r.kind != RowKind::Config || !r.editable) return false;
    if (r.type != ConfigMenuValueType::Bool) return false;
    r.boolCur = !r.boolCur;
    recomputeDirty_(r);
    formatValueText_(r);
    return true;
}

bool ConfigMenuModel::cycleSelect(uint8_t rowOnPage, int8_t direction)
{
    uint8_t idx = 0;
    if (!resolvePageRow_(rowOnPage, idx)) return false;
    Row& r = rows_[idx];
    if (r.kind != RowKind::Config || !r.editable) return false;
    if (r.widget != ConfigMenuWidget::Select || r.optionCount == 0) return false;

    int16_t cur = 0;
    bool found = false;
    for (uint8_t i = 0; i < r.optionCount; ++i) {
        if (strcmp(r.value, r.options[i]) == 0) {
            cur = i;
            found = true;
            break;
        }
    }
    if (!found) cur = 0;

    int16_t next = cur + direction;
    while (next < 0) next += r.optionCount;
    while (next >= r.optionCount) next -= r.optionCount;
    return setText(rowOnPage, r.options[next]);
}

bool ConfigMenuModel::setSlider(uint8_t rowOnPage, float value)
{
    uint8_t idx = 0;
    if (!resolvePageRow_(rowOnPage, idx)) return false;
    Row& r = rows_[idx];
    if (r.kind != RowKind::Config || !r.editable) return false;
    if (r.widget != ConfigMenuWidget::Slider) return false;
    if (r.type != ConfigMenuValueType::Int && r.type != ConfigMenuValueType::Float) return false;

    float v = value;
    if (v < r.sliderMin) v = r.sliderMin;
    if (v > r.sliderMax) v = r.sliderMax;
    if (r.sliderStep > 0.0f) {
        const float k = roundf((v - r.sliderMin) / r.sliderStep);
        v = r.sliderMin + (k * r.sliderStep);
    }

    if (r.type == ConfigMenuValueType::Int) r.intCur = (int32_t)lroundf(v);
    else r.floatCur = v;

    recomputeDirty_(r);
    formatValueText_(r);
    return true;
}

bool ConfigMenuModel::validate(char* ack, size_t ackLen)
{
    if (!cfgSvc_ || !cfgSvc_->applyJson || isHome()) return false;

    uint8_t dirtyCount = 0;
    for (uint8_t i = 0; i < rowCount_; ++i) {
        if (rows_[i].kind == RowKind::Config && rows_[i].dirty) ++dirtyCount;
    }

    if (dirtyCount == 0) {
        if (ack && ackLen > 0) snprintf(ack, ackLen, "{\"ok\":true,\"applied\":0}");
        return true;
    }

    DynamicJsonDocument patchDoc(Limits::JsonConfigApplyBuf);
    JsonObject root = patchDoc.to<JsonObject>();
    JsonObject moduleObj = root.createNestedObject(currentModule_);

    for (uint8_t i = 0; i < rowCount_; ++i) {
        const Row& r = rows_[i];
        if (r.kind != RowKind::Config || !r.dirty) continue;

        switch (r.type) {
            case ConfigMenuValueType::Bool: moduleObj[r.key] = r.boolCur; break;
            case ConfigMenuValueType::Int: moduleObj[r.key] = r.intCur; break;
            case ConfigMenuValueType::Float: moduleObj[r.key] = r.floatCur; break;
            case ConfigMenuValueType::Text: moduleObj[r.key] = r.textCur; break;
            default: break;
        }
    }

    char* payload = (char*)malloc(Limits::JsonConfigApplyBuf);
    if (!payload) return false;
    payload[0] = '\0';
    const size_t written = serializeJson(root, payload, Limits::JsonConfigApplyBuf);
    if (written == 0 || written >= Limits::JsonConfigApplyBuf) {
        free(payload);
        return false;
    }
    const bool ok = cfgSvc_->applyJson(cfgSvc_->ctx, payload);
    free(payload);
    if (!ok) {
        if (ack && ackLen > 0) snprintf(ack, ackLen, "{\"ok\":false,\"err\":\"apply\"}");
        return false;
    }

    char module[sizeof(currentModule_)]{};
    copyStr_(module, sizeof(module), currentModule_);
    if (!loadModule_(module)) {
        if (ack && ackLen > 0) snprintf(ack, ackLen, "{\"ok\":false,\"err\":\"reload\"}");
        return false;
    }

    if (ack && ackLen > 0) snprintf(ack, ackLen, "{\"ok\":true,\"applied\":%u}", (unsigned)dirtyCount);
    return true;
}

void ConfigMenuModel::buildView(ConfigMenuView& out) const
{
    out = ConfigMenuView{};

    buildBreadcrumb_(out.breadcrumb, sizeof(out.breadcrumb));
    out.pageIndex = pageIndex_;
    out.pageCount = pageCount();
    out.canHome = true;
    out.canBack = !isHome();
    out.canValidate = false;
    out.isHome = isHome();

    uint8_t visible = 0;
    for (uint8_t i = 0; i < RowsPerPage; ++i) {
        uint8_t idx = 0;
        if (!resolvePageRow_(i, idx)) break;
        const Row& r = rows_[idx];
        ConfigMenuRowView& vr = out.rows[i];
        vr.visible = true;
        vr.editable = r.editable;
        vr.dirty = r.dirty;
        vr.widget = r.widget;
        copyStr_(vr.key, sizeof(vr.key), r.key);
        copyStr_(vr.label, sizeof(vr.label), r.label);
        copyStr_(vr.value, sizeof(vr.value), r.value);
        ++visible;
    }
    out.rowCountOnPage = visible;

    if (!isHome()) {
        for (uint8_t i = 0; i < rowCount_; ++i) {
            if (rows_[i].kind == RowKind::Config && rows_[i].dirty) {
                out.canValidate = true;
                break;
            }
        }
    }
}

bool ConfigMenuModel::loadHome_()
{
    if (!cfgSvc_ || !cfgSvc_->listModules || !rows_ || !moduleList_) return false;

    const char* modulesRaw[MaxModules]{};
    moduleCount_ = cfgSvc_->listModules(cfgSvc_->ctx, modulesRaw, MaxModules);
    if (moduleCount_ > MaxModules) moduleCount_ = MaxModules;

    for (uint8_t i = 0; i < moduleCount_; ++i) {
        copyStr_(moduleList_[i], sizeof(moduleList_[i]), modulesRaw[i]);
    }

    for (uint8_t i = 0; i < moduleCount_; ++i) {
        for (uint8_t j = (uint8_t)(i + 1U); j < moduleCount_; ++j) {
            if (strcmp(moduleList_[i], moduleList_[j]) > 0) {
                char tmp[sizeof(moduleList_[i])]{};
                copyStr_(tmp, sizeof(tmp), moduleList_[i]);
                copyStr_(moduleList_[i], sizeof(moduleList_[i]), moduleList_[j]);
                copyStr_(moduleList_[j], sizeof(moduleList_[j]), tmp);
            }
        }
    }

    rowCount_ = 0;
    for (uint8_t i = 0; i < moduleCount_ && rowCount_ < MaxRows; ++i) {
        Row& r = rows_[rowCount_++];
        r = Row{};
        r.kind = RowKind::Module;
        r.editable = true;
        r.widget = ConfigMenuWidget::Text;
        copyStr_(r.module, sizeof(r.module), moduleList_[i]);
        copyStr_(r.key, sizeof(r.key), moduleList_[i]);
        copyStr_(r.label, sizeof(r.label), moduleList_[i]);
        copyStr_(r.value, sizeof(r.value), "open");
    }

    currentModule_[0] = '\0';
    pageIndex_ = 0;
    return true;
}

bool ConfigMenuModel::loadModule_(const char* module)
{
    if (!cfgSvc_ || !cfgSvc_->toJsonModule || !rows_) return false;
    if (!module || module[0] == '\0') return false;

    char jsonBuf[Limits::Mqtt::Buffers::StateCfg]{};
    bool truncated = false;
    const bool hasAny = cfgSvc_->toJsonModule(cfgSvc_->ctx, module, jsonBuf, sizeof(jsonBuf), &truncated);
    if (!hasAny || truncated) return false;

    DynamicJsonDocument doc(Limits::JsonConfigApplyBuf);
    const DeserializationError err = deserializeJson(doc, jsonBuf);
    if (err || !doc.is<JsonObjectConst>()) return false;

    JsonObjectConst obj = doc.as<JsonObjectConst>();

    rowCount_ = 0;
    for (JsonPairConst kv : obj) {
        if (rowCount_ >= MaxRows) break;
        const char* key = kv.key().c_str();
        JsonVariantConst value = kv.value();
        if (!key || key[0] == '\0') continue;

        Row row{};
        row.kind = RowKind::Config;
        row.editable = true;
        row.widget = ConfigMenuWidget::Text;
        copyStr_(row.module, sizeof(row.module), module);
        copyStr_(row.key, sizeof(row.key), key);
        copyStr_(row.label, sizeof(row.label), key);

        if (value.is<bool>()) {
            row.type = ConfigMenuValueType::Bool;
            row.boolCur = value.as<bool>();
            row.boolOrig = row.boolCur;
            row.widget = ConfigMenuWidget::Switch;
        } else if (value.is<int32_t>() || value.is<uint32_t>()) {
            row.type = ConfigMenuValueType::Int;
            row.intCur = value.as<int32_t>();
            row.intOrig = row.intCur;
        } else if (value.is<float>() || value.is<double>()) {
            row.type = ConfigMenuValueType::Float;
            row.floatCur = value.as<float>();
            row.floatOrig = row.floatCur;
        } else if (value.is<const char*>()) {
            row.type = ConfigMenuValueType::Text;
            copyStr_(row.textCur, sizeof(row.textCur), value.as<const char*>());
            copyStr_(row.textOrig, sizeof(row.textOrig), row.textCur);
        } else {
            continue;
        }

        applyHints_(row);
        recomputeDirty_(row);
        formatValueText_(row);
        rows_[rowCount_++] = row;
    }

    copyStr_(currentModule_, sizeof(currentModule_), module);
    pageIndex_ = 0;
    return true;
}

bool ConfigMenuModel::recomputeDirty_(Row& row)
{
    switch (row.type) {
        case ConfigMenuValueType::Bool: row.dirty = (row.boolCur != row.boolOrig); break;
        case ConfigMenuValueType::Int: row.dirty = (row.intCur != row.intOrig); break;
        case ConfigMenuValueType::Float: row.dirty = !sameFloat_(row.floatCur, row.floatOrig); break;
        case ConfigMenuValueType::Text: row.dirty = (strcmp(row.textCur, row.textOrig) != 0); break;
        default: row.dirty = false; break;
    }
    return row.dirty;
}

void ConfigMenuModel::formatValueText_(Row& row)
{
    switch (row.type) {
        case ConfigMenuValueType::Bool:
            copyStr_(row.value, sizeof(row.value), row.boolCur ? "ON" : "OFF");
            break;
        case ConfigMenuValueType::Int:
            snprintf(row.value, sizeof(row.value), "%ld", (long)row.intCur);
            break;
        case ConfigMenuValueType::Float:
            snprintf(row.value, sizeof(row.value), "%.3f", row.floatCur);
            trimFloat_(row.value);
            break;
        case ConfigMenuValueType::Text:
            copyStr_(row.value, sizeof(row.value), row.textCur);
            break;
        default:
            row.value[0] = '\0';
            break;
    }
}

const ConfigMenuHint* ConfigMenuModel::findHint_(const char* module, const char* key) const
{
    if (!hints_ || hintCount_ == 0 || !key) return nullptr;
    for (uint8_t i = 0; i < hintCount_; ++i) {
        const ConfigMenuHint& h = hints_[i];
        if (!h.key || strcmp(h.key, key) != 0) continue;
        if (h.module && module && strcmp(h.module, module) != 0) continue;
        if (h.module && !module) continue;
        return &h;
    }
    return nullptr;
}

bool ConfigMenuModel::parseOptions_(Row& row, const char* csv)
{
    row.optionCount = 0;
    if (!csv || csv[0] == '\0') return false;

    const char* p = csv;
    while (*p != '\0' && row.optionCount < (uint8_t)(sizeof(row.options) / sizeof(row.options[0]))) {
        char token[16]{};
        uint8_t n = 0;
        while (*p != '\0' && *p != '|' && n < (uint8_t)(sizeof(token) - 1U)) {
            token[n++] = *p++;
        }
        token[n] = '\0';
        if (n > 0) {
            copyStr_(row.options[row.optionCount], sizeof(row.options[row.optionCount]), token);
            ++row.optionCount;
        }
        if (*p == '|') ++p;
    }
    return row.optionCount > 0;
}

void ConfigMenuModel::applyHints_(Row& row)
{
    const ConfigMenuHint* hint = findHint_(row.module, row.key);
    if (!hint) return;

    row.widget = hint->constraints.widget;
    row.editable = hint->constraints.editable;

    if (row.widget == ConfigMenuWidget::Select) {
        if (!parseOptions_(row, hint->constraints.optionsCsv)) {
            row.widget = ConfigMenuWidget::Text;
        }
    }

    if (row.widget == ConfigMenuWidget::Slider &&
        (row.type == ConfigMenuValueType::Int || row.type == ConfigMenuValueType::Float)) {
        if (hint->constraints.maxValue > hint->constraints.minValue) {
            row.sliderMin = hint->constraints.minValue;
            row.sliderMax = hint->constraints.maxValue;
            row.sliderStep = hint->constraints.step > 0.0f ? hint->constraints.step : 1.0f;
        } else {
            row.widget = ConfigMenuWidget::Text;
        }
    }
}

bool ConfigMenuModel::appendText_(char* out, size_t outLen, size_t& pos, const char* text) const
{
    if (!out || outLen == 0 || !text) return false;
    const int n = snprintf(out + pos, outLen - pos, "%s", text);
    if (n <= 0) return false;
    pos += (size_t)n;
    if (pos >= outLen) {
        out[outLen - 1] = '\0';
        return false;
    }
    return true;
}

void ConfigMenuModel::buildBreadcrumb_(char* out, size_t outLen) const
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    size_t pos = 0;

    if (!appendText_(out, outLen, pos, "flow > cfg")) return;
    if (isHome()) return;
    if (!appendText_(out, outLen, pos, " > ")) return;

    const char* p = currentModule_;
    while (*p != '\0') {
        if (*p == '/') {
            if (!appendText_(out, outLen, pos, " > ")) return;
            ++p;
            continue;
        }
        char c[2]{*p, '\0'};
        if (!appendText_(out, outLen, pos, c)) return;
        ++p;
    }
}
