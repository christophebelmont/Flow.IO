/**
 * @file PoolLogicCommands.cpp
 * @brief Command handlers for PoolLogicModule.
 */

#include "PoolLogicModule.h"
#include "Core/CommandRegistry.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>
#include <stdio.h>

namespace {
// Commands may send either a compact args JSON or a full root payload with an
// "args" object. This helper accepts both shapes to preserve compatibility.
static bool parseCmdArgsObject_(const CommandRequest& req, JsonObjectConst& outObj)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::JsonCmdPoolDeviceBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;

    doc.clear();
    const char* json = req.args ? req.args : req.json;
    if (!json || json[0] == '\0') return false;

    const DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonObject>()) {
        outObj = doc.as<JsonObjectConst>();
        return true;
    }

    if (req.json && req.json[0] != '\0' && req.args != req.json) {
        doc.clear();
        const DeserializationError rootErr = deserializeJson(doc, req.json);
        if (rootErr || !doc.is<JsonObjectConst>()) return false;
        JsonVariantConst argsVar = doc["args"];
        if (argsVar.is<JsonObjectConst>()) {
            outObj = argsVar.as<JsonObjectConst>();
            return true;
        }
    }

    return false;
}

static bool parseBoolValue_(JsonVariantConst value, bool& out)
{
    if (value.is<bool>()) {
        out = value.as<bool>();
        return true;
    }
    if (value.is<int32_t>() || value.is<uint32_t>() || value.is<float>()) {
        out = (value.as<float>() != 0.0f);
        return true;
    }
    if (value.is<const char*>()) {
        const char* s = value.as<const char*>();
        if (!s) s = "0";
        if (strcmp(s, "true") == 0) out = true;
        else if (strcmp(s, "false") == 0) out = false;
        else out = (atoi(s) != 0);
        return true;
    }
    return false;
}

static void writeCmdError_(char* reply, size_t replyLen, const char* where, ErrorCode code)
{
    if (!writeErrorJson(reply, replyLen, code, where)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
}
}

// Thin static trampolines keep the command registry wiring stable while the
// actual logic lives in instance methods.
bool PoolLogicModule::cmdFiltrationWriteStatic_(void* userCtx,
                                                const CommandRequest& req,
                                                char* reply,
                                                size_t replyLen)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(userCtx);
    if (!self) return false;
    return self->cmdFiltrationWrite_(req, reply, replyLen);
}

bool PoolLogicModule::cmdAutoModeSetStatic_(void* userCtx,
                                            const CommandRequest& req,
                                            char* reply,
                                            size_t replyLen)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(userCtx);
    if (!self) return false;
    return self->cmdAutoModeSet_(req, reply, replyLen);
}

bool PoolLogicModule::cmdFiltrationRecalcStatic_(void* userCtx,
                                                 const CommandRequest& req,
                                                 char* reply,
                                                 size_t replyLen)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(userCtx);
    if (!self) return false;
    return self->cmdFiltrationRecalc_(req, reply, replyLen);
}

bool PoolLogicModule::cmdMqttControlStatic_(void* userCtx,
                                            const CommandRequest& req,
                                            char* reply,
                                            size_t replyLen)
{
    PoolLogicModule* self = static_cast<PoolLogicModule*>(userCtx);
    if (!self) return false;
    return self->cmdMqttControl_(req, reply, replyLen);
}

bool PoolLogicModule::cmdFiltrationWrite_(const CommandRequest& req, char* reply, size_t replyLen)
{
    if (!cfgStore_ || !poolSvc_ || !poolSvc_->writeDesired) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", ErrorCode::NotReady);
        return false;
    }

    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", ErrorCode::MissingArgs);
        return false;
    }
    if (!args.containsKey("value")) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", ErrorCode::MissingValue);
        return false;
    }

    bool requested = false;
    if (!parseBoolValue_(args["value"], requested)) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", ErrorCode::MissingValue);
        return false;
    }

    // A manual filtration command always disables auto mode explicitly so the
    // rest of the control loop stops fighting the operator's intent.
    (void)cfgStore_->set(autoModeVar_, false);
    autoMode_ = false;

    const PoolDeviceSvcStatus st = poolSvc_->writeDesired(poolSvc_->ctx, filtrationDeviceSlot_, requested ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        ErrorCode code = ErrorCode::Failed;
        if (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) code = ErrorCode::UnknownSlot;
        else if (st == POOLDEV_SVC_ERR_NOT_READY) code = ErrorCode::NotReady;
        else if (st == POOLDEV_SVC_ERR_DISABLED) code = ErrorCode::Disabled;
        else if (st == POOLDEV_SVC_ERR_INTERLOCK) code = ErrorCode::InterlockBlocked;
        else if (st == POOLDEV_SVC_ERR_IO) code = ErrorCode::IoError;
        writeCmdError_(reply, replyLen, "poollogic.filtration.write", code);
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u,\"value\":%s,\"auto_mode\":false}",
             (unsigned)filtrationDeviceSlot_,
             requested ? "true" : "false");
    return true;
}

bool PoolLogicModule::cmdFiltrationRecalc_(const CommandRequest&, char* reply, size_t replyLen)
{
    if (!enabled_) {
        writeCmdError_(reply, replyLen, "poollogic.filtration.recalc", ErrorCode::Disabled);
        return false;
    }

    // Match scheduler-trigger behavior: queue the recalc and let loop() own execution.
    portENTER_CRITICAL(&pendingMux_);
    pendingDailyRecalc_ = true;
    portEXIT_CRITICAL(&pendingMux_);

    snprintf(reply, replyLen, "{\"ok\":true,\"queued\":true}");
    return true;
}

bool PoolLogicModule::cmdAutoModeSet_(const CommandRequest& req, char* reply, size_t replyLen)
{
    if (!cfgStore_) {
        writeCmdError_(reply, replyLen, "poollogic.auto_mode.set", ErrorCode::NotReady);
        return false;
    }

    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "poollogic.auto_mode.set", ErrorCode::MissingArgs);
        return false;
    }
    if (!args.containsKey("value")) {
        writeCmdError_(reply, replyLen, "poollogic.auto_mode.set", ErrorCode::MissingValue);
        return false;
    }

    bool requested = false;
    if (!parseBoolValue_(args["value"], requested)) {
        writeCmdError_(reply, replyLen, "poollogic.auto_mode.set", ErrorCode::MissingValue);
        return false;
    }

    (void)cfgStore_->set(autoModeVar_, requested);
    autoMode_ = requested;

    snprintf(reply, replyLen, "{\"ok\":true,\"value\":%s}", requested ? "true" : "false");
    return true;
}

bool PoolLogicModule::cmdMqttControl_(const CommandRequest& req, char* reply, size_t replyLen)
{
    const char* cmdName = (req.cmd && req.cmd[0] != '\0') ? req.cmd : "poollogic.cmd";

    auto setModeValue = [&](const char* where,
                            ConfigVariable<bool, 0>& var,
                            bool& mirror) -> bool {
        if (!cfgStore_) {
            writeCmdError_(reply, replyLen, where, ErrorCode::NotReady);
            return false;
        }

        JsonObjectConst args;
        if (!parseCmdArgsObject_(req, args)) {
            writeCmdError_(reply, replyLen, where, ErrorCode::MissingArgs);
            return false;
        }
        if (!args.containsKey("value")) {
            writeCmdError_(reply, replyLen, where, ErrorCode::MissingValue);
            return false;
        }

        bool requested = false;
        if (!parseBoolValue_(args["value"], requested)) {
            writeCmdError_(reply, replyLen, where, ErrorCode::MissingValue);
            return false;
        }

        (void)cfgStore_->set(var, requested);
        mirror = requested;
        snprintf(reply, replyLen, "{\"ok\":true,\"value\":%s}", requested ? "true" : "false");
        return true;
    };

    auto toggleModeValue = [&](const char* where,
                               ConfigVariable<bool, 0>& var,
                               bool& mirror) -> bool {
        if (!cfgStore_) {
            writeCmdError_(reply, replyLen, where, ErrorCode::NotReady);
            return false;
        }

        const bool requested = !mirror;
        (void)cfgStore_->set(var, requested);
        mirror = requested;
        snprintf(reply, replyLen, "{\"ok\":true,\"value\":%s}", requested ? "true" : "false");
        return true;
    };

    auto writeDeviceValue = [&](const char* where,
                                uint8_t slot,
                                bool requested,
                                bool forceManualAutoMode,
                                const char* clearDosingModeKey) -> bool {
        if (!poolSvc_ || !poolSvc_->writeDesired) {
            writeCmdError_(reply, replyLen, where, ErrorCode::NotReady);
            return false;
        }

        if (forceManualAutoMode && cfgStore_) {
            (void)cfgStore_->set(autoModeVar_, false);
            autoMode_ = false;
        }

        // Keep behavior aligned with pooldevice.write: manual pump start disables
        // the corresponding auto mode so automation does not fight the operator.
        if (requested && clearDosingModeKey && cfgStore_) {
            char patch[96]{};
            snprintf(patch, sizeof(patch), "{\"poollogic/mode\":{\"%s\":false}}", clearDosingModeKey);
            if (cfgStore_->applyJson(patch)) {
                if (strcmp(clearDosingModeKey, "ph_auto_mode") == 0) phAutoMode_ = false;
                else if (strcmp(clearDosingModeKey, "orp_auto_mode") == 0) orpAutoMode_ = false;
            }
        }

        const PoolDeviceSvcStatus st = poolSvc_->writeDesired(poolSvc_->ctx, slot, requested ? 1U : 0U);
        if (st != POOLDEV_SVC_OK) {
            ErrorCode code = ErrorCode::Failed;
            if (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) code = ErrorCode::UnknownSlot;
            else if (st == POOLDEV_SVC_ERR_NOT_READY) code = ErrorCode::NotReady;
            else if (st == POOLDEV_SVC_ERR_DISABLED) code = ErrorCode::Disabled;
            else if (st == POOLDEV_SVC_ERR_INTERLOCK) code = ErrorCode::InterlockBlocked;
            else if (st == POOLDEV_SVC_ERR_IO) code = ErrorCode::IoError;
            writeCmdError_(reply, replyLen, where, code);
            return false;
        }

        if (forceManualAutoMode) {
            snprintf(reply,
                     replyLen,
                     "{\"ok\":true,\"slot\":%u,\"value\":%s,\"auto_mode\":false}",
                     (unsigned)slot,
                     requested ? "true" : "false");
        } else {
            snprintf(reply,
                     replyLen,
                     "{\"ok\":true,\"slot\":%u,\"value\":%s}",
                     (unsigned)slot,
                     requested ? "true" : "false");
        }
        return true;
    };

    auto writeDeviceFromArgs = [&](const char* where,
                                   uint8_t slot,
                                   bool forceManualAutoMode,
                                   const char* clearDosingModeKey) -> bool {
        JsonObjectConst args;
        if (!parseCmdArgsObject_(req, args)) {
            writeCmdError_(reply, replyLen, where, ErrorCode::MissingArgs);
            return false;
        }
        if (!args.containsKey("value")) {
            writeCmdError_(reply, replyLen, where, ErrorCode::MissingValue);
            return false;
        }
        bool requested = false;
        if (!parseBoolValue_(args["value"], requested)) {
            writeCmdError_(reply, replyLen, where, ErrorCode::MissingValue);
            return false;
        }
        return writeDeviceValue(where, slot, requested, forceManualAutoMode, clearDosingModeKey);
    };

    auto toggleDeviceValue = [&](const char* where,
                                 uint8_t slot,
                                 bool forceManualAutoMode,
                                 const char* clearDosingModeKey) -> bool {
        bool current = false;
        if (!readDeviceActualOn_(slot, current)) {
            writeCmdError_(reply, replyLen, where, ErrorCode::NotReady);
            return false;
        }
        return writeDeviceValue(where, slot, !current, forceManualAutoMode, clearDosingModeKey);
    };

    if (strcmp(cmdName, "poollogic.auto_mode.toggle") == 0) {
        return toggleModeValue("poollogic.auto_mode.toggle", autoModeVar_, autoMode_);
    }
    if (strcmp(cmdName, "poollogic.ph_auto_mode.set") == 0) {
        return setModeValue("poollogic.ph_auto_mode.set", phAutoModeVar_, phAutoMode_);
    }
    if (strcmp(cmdName, "poollogic.ph_auto_mode.toggle") == 0) {
        return toggleModeValue("poollogic.ph_auto_mode.toggle", phAutoModeVar_, phAutoMode_);
    }
    if (strcmp(cmdName, "poollogic.orp_auto_mode.set") == 0) {
        return setModeValue("poollogic.orp_auto_mode.set", orpAutoModeVar_, orpAutoMode_);
    }
    if (strcmp(cmdName, "poollogic.orp_auto_mode.toggle") == 0) {
        return toggleModeValue("poollogic.orp_auto_mode.toggle", orpAutoModeVar_, orpAutoMode_);
    }
    if (strcmp(cmdName, "poollogic.winter_mode.set") == 0) {
        return setModeValue("poollogic.winter_mode.set", winterModeVar_, winterMode_);
    }
    if (strcmp(cmdName, "poollogic.winter_mode.toggle") == 0) {
        return toggleModeValue("poollogic.winter_mode.toggle", winterModeVar_, winterMode_);
    }
    if (strcmp(cmdName, "poollogic.filtration.toggle") == 0) {
        return toggleDeviceValue("poollogic.filtration.toggle", filtrationDeviceSlot_, true, nullptr);
    }
    if (strcmp(cmdName, "poollogic.ph_pump.write") == 0) {
        return writeDeviceFromArgs("poollogic.ph_pump.write", phPumpDeviceSlot_, false, "ph_auto_mode");
    }
    if (strcmp(cmdName, "poollogic.ph_pump.toggle") == 0) {
        return toggleDeviceValue("poollogic.ph_pump.toggle", phPumpDeviceSlot_, false, "ph_auto_mode");
    }
    if (strcmp(cmdName, "poollogic.orp_pump.write") == 0) {
        return writeDeviceFromArgs("poollogic.orp_pump.write", orpPumpDeviceSlot_, false, "orp_auto_mode");
    }
    if (strcmp(cmdName, "poollogic.orp_pump.toggle") == 0) {
        return toggleDeviceValue("poollogic.orp_pump.toggle", orpPumpDeviceSlot_, false, "orp_auto_mode");
    }
    if (strcmp(cmdName, "poollogic.light.write") == 0 || strcmp(cmdName, "poollogic.lights.write") == 0) {
        return writeDeviceFromArgs("poollogic.lights.write", PoolBinding::kDeviceSlotLights, false, nullptr);
    }
    if (strcmp(cmdName, "poollogic.light.toggle") == 0 || strcmp(cmdName, "poollogic.lights.toggle") == 0) {
        return toggleDeviceValue("poollogic.lights.toggle", PoolBinding::kDeviceSlotLights, false, nullptr);
    }
    if (strcmp(cmdName, "poollogic.robot.write") == 0) {
        return writeDeviceFromArgs("poollogic.robot.write", robotDeviceSlot_, false, nullptr);
    }
    if (strcmp(cmdName, "poollogic.robot.toggle") == 0) {
        return toggleDeviceValue("poollogic.robot.toggle", robotDeviceSlot_, false, nullptr);
    }
    if (strcmp(cmdName, "poollogic.heater.write") == 0) {
        return writeDeviceFromArgs("poollogic.heater.write", PoolBinding::kDeviceSlotWaterHeater, false, nullptr);
    }
    if (strcmp(cmdName, "poollogic.heater.toggle") == 0) {
        return toggleDeviceValue("poollogic.heater.toggle", PoolBinding::kDeviceSlotWaterHeater, false, nullptr);
    }
    if (strcmp(cmdName, "poollogic.chlorine_generator.write") == 0 || strcmp(cmdName, "poollogic.swg.write") == 0) {
        return writeDeviceFromArgs("poollogic.chlorine_generator.write", swgDeviceSlot_, false, nullptr);
    }
    if (strcmp(cmdName, "poollogic.chlorine_generator.toggle") == 0 || strcmp(cmdName, "poollogic.swg.toggle") == 0) {
        return toggleDeviceValue("poollogic.chlorine_generator.toggle", swgDeviceSlot_, false, nullptr);
    }

    writeCmdError_(reply, replyLen, cmdName, ErrorCode::UnknownCmd);
    return false;
}
