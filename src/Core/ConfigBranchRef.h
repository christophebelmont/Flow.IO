#pragma once
/**
 * @file ConfigBranchRef.h
 * @brief Compact 8/8 branch identity shared across config components.
 */

#include <stdint.h>

struct ConfigBranchRef {
    static constexpr uint8_t UnknownModule = 0;
    static constexpr uint8_t UnknownLocalBranch = 0;

    uint8_t moduleId = UnknownModule;
    uint8_t localBranchId = UnknownLocalBranch;
};

inline constexpr bool configBranchRefIsKnown(ConfigBranchRef ref)
{
    return ref.moduleId != ConfigBranchRef::UnknownModule &&
           ref.localBranchId != ConfigBranchRef::UnknownLocalBranch;
}

inline constexpr bool configBranchRefEqual(ConfigBranchRef a, ConfigBranchRef b)
{
    return a.moduleId == b.moduleId && a.localBranchId == b.localBranchId;
}

inline constexpr uint16_t packConfigBranchRef(ConfigBranchRef ref)
{
    return (uint16_t)(((uint16_t)ref.moduleId << 8) | (uint16_t)ref.localBranchId);
}

inline constexpr ConfigBranchRef unpackConfigBranchRef(uint16_t packed)
{
    ConfigBranchRef out{};
    out.moduleId = (uint8_t)(packed >> 8);
    out.localBranchId = (uint8_t)(packed & 0xFFU);
    return out;
}
