#pragma once
#include "Packet.h"

namespace network {
constexpr std::uint16_t kGameVersion = 1;
constexpr std::uint16_t kProtocolVersion = 5;
enum class ErrorCode : std::uint16_t { RoomFull = 1, BadRequest = 2, VersionMismatch = 3, Timeout = 4 };

inline std::vector<std::uint8_t> HelloPayload() {
    return {static_cast<std::uint8_t>(kGameVersion >> 8),
            static_cast<std::uint8_t>(kGameVersion),
            static_cast<std::uint8_t>(kProtocolVersion >> 8),
            static_cast<std::uint8_t>(kProtocolVersion)};
}
inline std::uint16_t PayloadWord(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((std::uint16_t(p[0]) << 8) | p[1]);
}

constexpr std::uint8_t kInputNone = 0x00;
constexpr std::uint8_t kInputUp = 0x01;
constexpr std::uint8_t kInputDown = 0x02;
constexpr std::uint8_t kInputLeft = 0x04;
constexpr std::uint8_t kInputRight = 0x08;

struct InputData {
    std::uint8_t directionMask = kInputNone;
    std::uint32_t sequence = 0;
};

inline const char* InputDirectionText(std::uint8_t directionMask) {
    switch (directionMask) {
    case kInputNone: return "NONE";
    case kInputUp: return "UP";
    case kInputDown: return "DOWN";
    case kInputLeft: return "LEFT";
    case kInputRight: return "RIGHT";
    default: return nullptr; // Combined directions are not supported at this stage.
    }
}

// Five wire bytes: direction mask, followed by a big-endian uint32 sequence.
// Player identity comes from the approved Session, never from this payload.
inline std::vector<std::uint8_t> InputPayload(std::uint8_t directionMask,
                                            std::uint32_t sequence) {
    if (!InputDirectionText(directionMask)) return {};
    return {directionMask, static_cast<std::uint8_t>(sequence >> 24),
            static_cast<std::uint8_t>(sequence >> 16),
            static_cast<std::uint8_t>(sequence >> 8), static_cast<std::uint8_t>(sequence)};
}

inline bool ReadInputPayload(const std::vector<std::uint8_t>& payload, InputData& output) {
    if (payload.size() != 5 || !InputDirectionText(payload[0])) return false;
    output.directionMask = payload[0];
    output.sequence = (std::uint32_t(PayloadWord(payload.data() + 1)) << 16) |
                      PayloadWord(payload.data() + 3);
    return true;
}

inline std::vector<std::uint8_t> ErrorPayload(ErrorCode code) {
    const auto value = static_cast<std::uint16_t>(code);
    return {static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
}
inline const char* ErrorText(ErrorCode code) {
    switch (code) {
    case ErrorCode::RoomFull: return "Room full";
    case ErrorCode::BadRequest: return "Invalid request";
    case ErrorCode::VersionMismatch: return "Version mismatch";
    case ErrorCode::Timeout: return "HELLO timeout";
    default: return nullptr;
    }
}
} // namespace network
