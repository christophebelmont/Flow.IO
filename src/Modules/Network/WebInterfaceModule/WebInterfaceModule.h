#pragma once
/**
 * @file WebInterfaceModule.h
 * @brief Web interface module for Supervisor profile.
 */

#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Core/Services/ILogger.h"
#include "Core/NvsKeys.h"

#include <HardwareSerial.h>
#include <ESPAsyncWebServer.h>
#include "Core/EventBus/EventBus.h"

class WebInterfaceModule : public Module {
public:
    const char* moduleId() const override { return "webinterface"; }
    const char* taskName() const override { return "webinterface"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 4096; }

    uint8_t dependencyCount() const override { return 6; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "wifi";
        if (i == 2) return "eventbus";
        if (i == 3) return "datastore";
        if (i == 4) return "cmd";
        if (i == 5) return "i2ccfg.client";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static constexpr int kServerPort = 80;

#ifndef FLOW_SUPERVISOR_WEBSERIAL_BAUD
    static constexpr uint32_t kUartBaud = 115200;
#else
    static constexpr uint32_t kUartBaud = FLOW_SUPERVISOR_WEBSERIAL_BAUD;
#endif

#ifndef FLOW_SUPERVISOR_WEBSERIAL_RX
    static constexpr int kUartRxPin = 16;
#else
    static constexpr int kUartRxPin = FLOW_SUPERVISOR_WEBSERIAL_RX;
#endif

#ifndef FLOW_SUPERVISOR_WEBSERIAL_TX
    static constexpr int kUartTxPin = 17;
#else
    static constexpr int kUartTxPin = FLOW_SUPERVISOR_WEBSERIAL_TX;
#endif

    // Keep UART framing aligned with core log entry limits.
    static constexpr size_t kSerialLogLineChars =
        (size_t)LOG_MSG_MAX + (size_t)LOG_TAG_MAX + 64U;
    static constexpr size_t kLineBufferSize = kSerialLogLineChars * 4U;
    static constexpr size_t kUartRxBufferSize = kLineBufferSize * 2U;

    HardwareSerial& uart_ = Serial2;
    AsyncWebServer server_{kServerPort};
    AsyncWebSocket ws_{"/wsserial"};

    const LogHubService* logHub_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const FlowCfgRemoteService* flowCfgSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    EventBus* eventBus_ = nullptr;
    bool started_ = false;
    bool spiffsReady_ = false;
    volatile bool netReady_ = false;
    volatile bool uartPaused_ = false;
    const FirmwareUpdateService* fwUpdateSvc_ = nullptr;
    ServiceRegistry* services_ = nullptr;

    struct MqttWebConfigData {
        char host[Limits::Mqtt::Buffers::Host] = "192.168.86.250";
        int32_t port = Limits::Mqtt::Defaults::Port;
        char user[Limits::Mqtt::Buffers::User] = "";
        char pass[Limits::Mqtt::Buffers::Pass] = "";
    } mqttCfg_{};

    ConfigVariable<char, 2> mqttHostVar_{
        NVS_KEY(NvsKeys::Mqtt::Host), "host", "mqtt",
        ConfigType::CharArray, mqttCfg_.host, ConfigPersistence::Persistent, sizeof(mqttCfg_.host)
    };
    ConfigVariable<int32_t, 2> mqttPortVar_{
        NVS_KEY(NvsKeys::Mqtt::Port), "port", "mqtt",
        ConfigType::Int32, &mqttCfg_.port, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<char, 2> mqttUserVar_{
        NVS_KEY(NvsKeys::Mqtt::User), "user", "mqtt",
        ConfigType::CharArray, mqttCfg_.user, ConfigPersistence::Persistent, sizeof(mqttCfg_.user)
    };
    ConfigVariable<char, 2> mqttPassVar_{
        NVS_KEY(NvsKeys::Mqtt::Pass), "pass", "mqtt",
        ConfigType::CharArray, mqttCfg_.pass, ConfigPersistence::Persistent, sizeof(mqttCfg_.pass)
    };

    char lineBuf_[kLineBufferSize] = {0};
    size_t lineLen_ = 0;

    void startServer_();
    void onWsEvent_(AsyncWebSocket* server,
                    AsyncWebSocketClient* client,
                    AwsEventType type,
                    void* arg,
                    uint8_t* data,
                    size_t len);
    void handleUpdateRequest_(AsyncWebServerRequest* request, FirmwareUpdateTarget target);
    bool isWebReachable_() const;
    bool getNetworkIp_(char* out, size_t len, NetworkAccessMode* modeOut) const;
    void flushLine_(bool force);
    static bool isLogByte_(uint8_t c);
    static bool svcSetPaused_(void* ctx, bool paused);
    static bool svcIsPaused_(void* ctx);
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
};
