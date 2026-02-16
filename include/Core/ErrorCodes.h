#pragma once
/**
 * @file ErrorCodes.h
 * @brief Shared error codes and JSON error payload formatting helpers.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "Core/Log.h"

enum class ErrorCode : uint16_t {
    UnknownCmd = 0,
    BadCmdJson,
    MissingCmd,
    CmdServiceUnavailable,
    ArgsTooLarge,
    CmdHandlerFailed,
    BadCfgJson,
    CfgServiceUnavailable,
    CfgApplyFailed,
    UnknownTopic,
    InternalAckOverflow,
    CfgTruncated,
    MissingArgs,
    MissingSlot,
    BadSlot,
    MissingValue,
    UnknownSlot,
    NotReady,
    Disabled,
    InterlockBlocked,
    IoError,
    Failed,
    InvalidSlot,
    UnusedSlot,
    ReservedSlot,
    InvalidEventId,
    MissingEventId,
    InvalidMode,
    InvalidBool,
    InvalidWeekdayMask,
    InvalidStartHour,
    InvalidStartMinute,
    InvalidEndHour,
    InvalidEndMinute,
    InvalidStartEpoch,
    InvalidEndEpoch,
    InvalidLabel,
    SetFailed,
    ClearFailed,
    ClearAllFailed
};

static inline const char* errorCodeStr(ErrorCode code)
{
    switch (code) {
    case ErrorCode::UnknownCmd: return "UnknownCmd";
    case ErrorCode::BadCmdJson: return "BadCmdJson";
    case ErrorCode::MissingCmd: return "MissingCmd";
    case ErrorCode::CmdServiceUnavailable: return "CmdServiceUnavailable";
    case ErrorCode::ArgsTooLarge: return "ArgsTooLarge";
    case ErrorCode::CmdHandlerFailed: return "CmdHandlerFailed";
    case ErrorCode::BadCfgJson: return "BadCfgJson";
    case ErrorCode::CfgServiceUnavailable: return "CfgServiceUnavailable";
    case ErrorCode::CfgApplyFailed: return "CfgApplyFailed";
    case ErrorCode::UnknownTopic: return "UnknownTopic";
    case ErrorCode::InternalAckOverflow: return "InternalAckOverflow";
    case ErrorCode::CfgTruncated: return "CfgTruncated";
    case ErrorCode::MissingArgs: return "MissingArgs";
    case ErrorCode::MissingSlot: return "MissingSlot";
    case ErrorCode::BadSlot: return "BadSlot";
    case ErrorCode::MissingValue: return "MissingValue";
    case ErrorCode::UnknownSlot: return "UnknownSlot";
    case ErrorCode::NotReady: return "NotReady";
    case ErrorCode::Disabled: return "Disabled";
    case ErrorCode::InterlockBlocked: return "InterlockBlocked";
    case ErrorCode::IoError: return "IoError";
    case ErrorCode::Failed: return "Failed";
    case ErrorCode::InvalidSlot: return "InvalidSlot";
    case ErrorCode::UnusedSlot: return "UnusedSlot";
    case ErrorCode::ReservedSlot: return "ReservedSlot";
    case ErrorCode::InvalidEventId: return "InvalidEventId";
    case ErrorCode::MissingEventId: return "MissingEventId";
    case ErrorCode::InvalidMode: return "InvalidMode";
    case ErrorCode::InvalidBool: return "InvalidBool";
    case ErrorCode::InvalidWeekdayMask: return "InvalidWeekdayMask";
    case ErrorCode::InvalidStartHour: return "InvalidStartHour";
    case ErrorCode::InvalidStartMinute: return "InvalidStartMinute";
    case ErrorCode::InvalidEndHour: return "InvalidEndHour";
    case ErrorCode::InvalidEndMinute: return "InvalidEndMinute";
    case ErrorCode::InvalidStartEpoch: return "InvalidStartEpoch";
    case ErrorCode::InvalidEndEpoch: return "InvalidEndEpoch";
    case ErrorCode::InvalidLabel: return "InvalidLabel";
    case ErrorCode::SetFailed: return "SetFailed";
    case ErrorCode::ClearFailed: return "ClearFailed";
    case ErrorCode::ClearAllFailed: return "ClearAllFailed";
    default: return "Unknown";
    }
}

static inline bool errorCodeRetryable(ErrorCode code)
{
    switch (code) {
    case ErrorCode::CmdServiceUnavailable:
    case ErrorCode::CfgServiceUnavailable:
    case ErrorCode::NotReady:
    case ErrorCode::IoError:
    case ErrorCode::InternalAckOverflow:
    case ErrorCode::CfgTruncated:
        return true;
    default:
        return false;
    }
}

static inline bool writeErrorJson(char* out, size_t outLen, ErrorCode code, const char* where)
{
    if (!out || outLen == 0) return false;
    const char* w = (where && where[0] != '\0') ? where : "unknown";
    const int wrote = snprintf(
        out,
        outLen,
        "{\"ok\":false,\"err\":{\"code\":\"%s\",\"where\":\"%s\",\"retryable\":%s}}",
        errorCodeStr(code),
        w,
        errorCodeRetryable(code) ? "true" : "false"
    );
    const bool ok = (wrote > 0) && ((size_t)wrote < outLen);
    if (!ok) {
        Log::warn("ErrCodes", "writeErrorJson truncated (len=%u wrote=%d where=%s)",
                  (unsigned)outLen, wrote, w);
    }
    return ok;
}

static inline bool writeOkJson(char* out, size_t outLen, const char* where)
{
    if (!out || outLen == 0) return false;
    const char* w = (where && where[0] != '\0') ? where : "unknown";
    const int wrote = snprintf(
        out,
        outLen,
        "{\"ok\":true,\"where\":\"%s\"}",
        w
    );
    const bool ok = (wrote > 0) && ((size_t)wrote < outLen);
    if (!ok) {
        Log::warn("ErrCodes", "writeOkJson truncated (len=%u wrote=%d where=%s)",
                  (unsigned)outLen, wrote, w);
    }
    return ok;
}

static inline bool writeErrorJsonWithSlot(char* out, size_t outLen, ErrorCode code, const char* where, uint8_t slot)
{
    if (!out || outLen == 0) return false;
    const char* w = (where && where[0] != '\0') ? where : "unknown";
    const int wrote = snprintf(
        out,
        outLen,
        "{\"ok\":false,\"slot\":%u,\"err\":{\"code\":\"%s\",\"where\":\"%s\",\"retryable\":%s}}",
        (unsigned)slot,
        errorCodeStr(code),
        w,
        errorCodeRetryable(code) ? "true" : "false"
    );
    const bool ok = (wrote > 0) && ((size_t)wrote < outLen);
    if (!ok) {
        Log::warn("ErrCodes", "writeErrorJsonWithSlot truncated (len=%u wrote=%d where=%s slot=%u)",
                  (unsigned)outLen, wrote, w, (unsigned)slot);
    }
    return ok;
}
