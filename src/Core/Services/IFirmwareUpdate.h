#pragma once
/**
 * @file IFirmwareUpdate.h
 * @brief Firmware update service interface (Supervisor profile).
 */

#include <stddef.h>
#include <stdint.h>

enum class FirmwareUpdateTarget : uint8_t {
    FlowIO = 1,
    Nextion = 2,
    Supervisor = 3
};

struct FirmwareUpdateService {
    bool (*start)(void* ctx, FirmwareUpdateTarget target, const char* url, char* errOut, size_t errOutLen);
    bool (*statusJson)(void* ctx, char* out, size_t outLen);
    bool (*configJson)(void* ctx, char* out, size_t outLen);
    bool (*setConfig)(void* ctx,
                      const char* updateHost,
                      const char* flowioPath,
                      const char* supervisorPath,
                      const char* nextionPath,
                      char* errOut,
                      size_t errOutLen);
    void* ctx;
};
