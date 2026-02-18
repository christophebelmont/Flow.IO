/**
 * @file HMIModule.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/HMIModule.h"

#include "Board/BoardSerialMap.h"
#include "Core/EventBus/EventPayloads.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#define LOG_TAG "HMIModul"
#include "Core/ModuleLog.h"

namespace {

static const ConfigMenuHint kHints[] = {
    {"poollogic", "filtr_start_min", {ConfigMenuWidget::Slider, true, 0.0f, 23.0f, 1.0f, nullptr}},
    {"poollogic", "filtr_stop_max", {ConfigMenuWidget::Slider, true, 0.0f, 23.0f, 1.0f, nullptr}},
    {"poollogic", "ph_setpoint", {ConfigMenuWidget::Slider, true, 6.6f, 7.8f, 0.1f, nullptr}},
    {"poollogic", "orp_setpoint", {ConfigMenuWidget::Slider, true, 450.0f, 950.0f, 10.0f, nullptr}},
    {"time", "tz", {ConfigMenuWidget::Select, true, 0.0f, 0.0f, 1.0f,
                    "CET-1CEST,M3.5.0/2,M10.5.0/3|UTC0|EST5EDT,M3.2.0/2,M11.1.0/2"}}
};

} // namespace

bool HMIModule::svcRequestRefresh_(void* ctx)
{
    HMIModule* self = static_cast<HMIModule*>(ctx);
    if (!self) return false;
    self->viewDirty_ = true;
    return true;
}

bool HMIModule::svcOpenConfigHome_(void* ctx)
{
    HMIModule* self = static_cast<HMIModule*>(ctx);
    if (!self) return false;
    const bool ok = self->menu_.home();
    if (ok) self->viewDirty_ = true;
    return ok;
}

bool HMIModule::svcOpenConfigModule_(void* ctx, const char* module)
{
    HMIModule* self = static_cast<HMIModule*>(ctx);
    if (!self) return false;
    const bool ok = self->menu_.openModule(module);
    if (ok) self->viewDirty_ = true;
    return ok;
}

bool HMIModule::svcBuildConfigMenuJson_(void* ctx, char* out, size_t outLen)
{
    const HMIModule* self = static_cast<const HMIModule*>(ctx);
    if (!self) return false;
    return self->buildMenuJson_(out, outLen);
}

void HMIModule::init(ConfigStore&, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>("loghub");
    cfgSvc_ = services.get<ConfigStoreService>("config");
    auto* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    if (!cfgSvc_) {
        LOGE("Config service unavailable");
        return;
    }

    const bool okMenu = menu_.begin(cfgSvc_);
    menu_.setHints(kHints, (uint8_t)(sizeof(kHints) / sizeof(kHints[0])));
    if (!okMenu) {
        LOGE("Config menu init failed");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &HMIModule::onEventStatic_, this);
    }

    static HmiService svc{
        svcRequestRefresh_,
        svcOpenConfigHome_,
        svcOpenConfigModule_,
        svcBuildConfigMenuJson_,
        nullptr
    };
    svc.ctx = this;
    services.add("hmi", &svc);

    NextionDriverConfig dcfg{};
    dcfg.serial = &Board::SerialMap::hmiSerial();
    dcfg.rxPin = Board::SerialMap::hmiRxPin();
    dcfg.txPin = Board::SerialMap::hmiTxPin();
    dcfg.baud = Board::SerialMap::HmiBaud;
    nextion_.setConfig(dcfg);
    driver_ = &nextion_;
    driverReady_ = false;
    viewDirty_ = true;
    lastRenderMs_ = 0;

    LOGI("HMI service registered with driver=%s", driver_ ? driver_->driverId() : "none");
}

void HMIModule::onEventStatic_(const Event& e, void* user)
{
    HMIModule* self = static_cast<HMIModule*>(user);
    if (self) self->onEvent_(e);
}

bool HMIModule::refreshCurrentModule_()
{
    const uint8_t prevPage = menu_.pageIndex();
    if (!menu_.refreshCurrent()) return false;
    while (menu_.pageIndex() < prevPage && menu_.nextPage()) {
    }
    return true;
}

void HMIModule::onEvent_(const Event& e)
{
    if (e.id != EventId::ConfigChanged || !e.payload || e.len < sizeof(ConfigChangedPayload)) return;
    if (menu_.isHome()) {
        viewDirty_ = true;
        return;
    }

    const ConfigChangedPayload* p = static_cast<const ConfigChangedPayload*>(e.payload);
    if (!p->module[0]) return;
    if (strcmp(p->module, menu_.currentModule()) != 0) return;

    if (refreshCurrentModule_()) {
        viewDirty_ = true;
    }
}

void HMIModule::handleDriverEvent_(const HmiEvent& e)
{
    bool changed = false;
    switch (e.type) {
        case HmiEventType::Home:
            changed = menu_.home();
            break;
        case HmiEventType::Back:
            changed = menu_.back();
            break;
        case HmiEventType::Validate: {
            char ack[96]{};
            changed = menu_.validate(ack, sizeof(ack));
            if (!changed) {
                LOGW("Validate failed");
            }
            break;
        }
        case HmiEventType::NextPage:
            changed = menu_.nextPage();
            break;
        case HmiEventType::PrevPage:
            changed = menu_.prevPage();
            break;
        case HmiEventType::RowActivate: {
            changed = menu_.enterRow(e.row);
            if (!changed) {
                ConfigMenuView view{};
                menu_.buildView(view);
                if (e.row < ConfigMenuModel::RowsPerPage && view.rows[e.row].visible) {
                    const ConfigMenuWidget widget = view.rows[e.row].widget;
                    if (widget == ConfigMenuWidget::Switch) changed = menu_.toggleSwitch(e.row);
                    else if (widget == ConfigMenuWidget::Select) changed = menu_.cycleSelect(e.row, 1);
                }
            }
            break;
        }
        case HmiEventType::RowToggle:
            changed = menu_.toggleSwitch(e.row);
            break;
        case HmiEventType::RowCycle:
            changed = menu_.cycleSelect(e.row, e.direction);
            break;
        case HmiEventType::RowSetText:
            changed = menu_.setText(e.row, e.text);
            break;
        case HmiEventType::RowSetSlider:
            changed = menu_.setSlider(e.row, e.sliderValue);
            break;
        case HmiEventType::None:
        default:
            break;
    }

    if (changed) viewDirty_ = true;
}

bool HMIModule::render_()
{
    if (!driver_) return false;
    ConfigMenuView view{};
    menu_.buildView(view);
    const bool ok = driver_->renderConfigMenu(view);
    if (ok) {
        lastRenderMs_ = millis();
    }
    return ok;
}

bool HMIModule::buildMenuJson_(char* out, size_t outLen) const
{
    if (!out || outLen == 0) return false;

    ConfigMenuView view{};
    menu_.buildView(view);

    DynamicJsonDocument doc(2048);
    JsonObject root = doc.to<JsonObject>();
    root["ok"] = true;
    root["driver"] = driver_ ? driver_->driverId() : "";
    root["path"] = view.breadcrumb;
    root["page"] = (uint32_t)view.pageIndex + 1U;
    root["pages"] = (uint32_t)view.pageCount;
    root["rows"] = (uint32_t)view.rowCountOnPage;
    root["can_home"] = view.canHome;
    root["can_back"] = view.canBack;
    root["can_validate"] = view.canValidate;

    JsonArray arr = root.createNestedArray("items");
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        const ConfigMenuRowView& row = view.rows[i];
        if (!row.visible) continue;
        JsonObject it = arr.createNestedObject();
        it["i"] = i;
        it["key"] = row.key;
        it["label"] = row.label;
        it["value"] = row.value;
        it["editable"] = row.editable;
        it["dirty"] = row.dirty;
        it["widget"] = (uint8_t)row.widget;
    }

    const size_t written = serializeJson(root, out, outLen);
    return written > 0 && written < outLen;
}

void HMIModule::loop()
{
    if (!driver_) {
        vTaskDelay(pdMS_TO_TICKS(250));
        return;
    }

    if (!driverReady_) {
        driverReady_ = driver_->begin();
        if (!driverReady_) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
        viewDirty_ = true;
    }

    HmiEvent ev{};
    while (driver_->pollEvent(ev)) {
        handleDriverEvent_(ev);
    }

    const uint32_t now = millis();
    const bool periodicRefresh = ((uint32_t)(now - lastRenderMs_) >= 1200U);
    if (viewDirty_ || periodicRefresh) {
        if (render_()) {
            viewDirty_ = false;
        }
    }

    driver_->tick(now);
    vTaskDelay(pdMS_TO_TICKS(25));
}
