#pragma once
/**
 * @file ConfigTypes.h
 * @brief Shared configuration types and metadata.
 */
#include <stdint.h>
#include <stddef.h>
#include "Core/SystemLimits.h"

// Max NVS key length for Preferences (excluding null terminator).
// Always wrap literal NVS keys with NVS_KEY("...") to enforce length at compile time.
template <size_t N>
constexpr const char* NVS_KEY(const char (&s)[N]) {
    static_assert(N > 0, "NVS key cannot be empty");
    static_assert((N - 1) <= Limits::MaxNvsKeyLen, "NVS key too long");
    return s;
}

/** @brief Config persistence mode. */
enum class ConfigPersistence : uint8_t { Runtime, Persistent };

/** @brief Supported config value types. */
enum class ConfigType : uint8_t {
    Int32,
    UInt8,
    Bool,
    Float,
    Double,
    CharArray
};

/** @brief Callback signature for config changes. */
template<typename T>
using ConfigCallback = void(*)(void* ctx, const T& value);

/**
 * @brief Declares a config variable and optional change handlers.
 */
template<typename T, size_t MAX_HANDLERS>
struct ConfigVariable {
    const char* nvsKey;
    const char* jsonName;
    const char* moduleName;
    ConfigType type;
    T* value;
    ConfigPersistence persistence;
    uint16_t size; // for char[]

    /** @brief Change handler entry. */
    struct Handler { ConfigCallback<T> cb; void* ctx; };
    Handler handlers[MAX_HANDLERS];
    uint8_t handlerCount = 0;

    /** @brief Register a change handler. */
    bool addHandler(ConfigCallback<T> cb, void* ctx) {
        if (handlerCount >= MAX_HANDLERS) return false;
        handlers[handlerCount++] = {cb, ctx};
        return true;
    }

    /** @brief Notify all handlers with the current value. */
    void notify() {
        for (uint8_t i = 0; i < handlerCount; ++i)
            handlers[i].cb(handlers[i].ctx, *value);
    }
};

/** @brief Internal metadata for registered variables. */
struct ConfigMeta {
    const char* module;
    const char* name;
    const char* nvsKey;
    ConfigType type;
    ConfigPersistence persistence;
    void* valuePtr;
    uint16_t size;
};
