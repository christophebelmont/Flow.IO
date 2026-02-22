#pragma once
/**
 * @file I2cCfgProtocol.h
 * @brief Shared I2C protocol constants for Flow.IO cfg remote access.
 */

#include <stddef.h>
#include <stdint.h>

namespace I2cCfgProtocol {

constexpr uint8_t ReqMagic = 0xA5;
constexpr uint8_t RespMagic = 0x5A;
constexpr uint8_t Version = 1;

constexpr size_t MaxPayload = 96;
constexpr size_t ReqHeaderSize = 5;   // magic, ver, op, seq, payload_len
constexpr size_t RespHeaderSize = 6;  // magic, ver, op, seq, status, payload_len
constexpr size_t MaxReqFrame = ReqHeaderSize + MaxPayload;
constexpr size_t MaxRespFrame = RespHeaderSize + MaxPayload;

enum Op : uint8_t {
    OpPing = 0x01,
    OpListCount = 0x10,
    OpListItem = 0x11,
    OpListChildrenCount = 0x12,
    OpListChildrenItem = 0x13,
    OpGetModuleBegin = 0x20,
    OpGetModuleChunk = 0x21,
    OpGetRuntimeStatusBegin = 0x22,
    OpGetRuntimeStatusChunk = 0x23,
    OpPatchBegin = 0x30,
    OpPatchWrite = 0x31,
    OpPatchCommit = 0x32,
    OpSystemAction = 0x40
};

enum Status : uint8_t {
    StatusOk = 0,
    StatusBadRequest = 1,
    StatusNotReady = 2,
    StatusRange = 3,
    StatusOverflow = 4,
    StatusFailed = 5
};

}  // namespace I2cCfgProtocol
