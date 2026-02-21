#pragma once
/**
 * @file IFlowCfg.h
 * @brief Supervisor-facing service for remote Flow.IO config over I2C.
 */

#include <stddef.h>

struct FlowCfgRemoteService {
    bool (*isReady)(void* ctx);
    bool (*listModulesJson)(void* ctx, char* out, size_t outLen);
    bool (*listChildrenJson)(void* ctx, const char* prefix, char* out, size_t outLen);
    bool (*getModuleJson)(void* ctx, const char* module, char* out, size_t outLen, bool* truncated);
    bool (*runtimeStatusJson)(void* ctx, char* out, size_t outLen);
    bool (*applyPatchJson)(void* ctx, const char* patch, char* out, size_t outLen);
    void* ctx;
};
