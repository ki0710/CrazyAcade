#pragma once
#include "Packet.h"

namespace network {
constexpr std::uint16_t kGameVersion = 1;
constexpr std::uint16_t kProtocolVersion = 12;
constexpr int kLoadingTimeoutSeconds = 10;
enum class CancelReason : std::uint8_t { PeerLeft = 1, Timeout = 2, Unready = 3 };
enum class ErrorCode : std::uint16_t { RoomFull = 1, BadRequest = 2, VersionMismatch = 3, Timeout = 4,
    SelectionDenied = 5 };
enum class SelectKind : std::uint8_t { Map = 1, Color = 2 };
// Existing game values: Forest=1, City=2, Ice=3; Player colorHueTable indices 0..6.
inline bool ValidMap(std::uint8_t value) { return value >= 1 && value <= 3; }
inline bool ValidColor(std::uint8_t value) { return value <= 6; }
inline bool ValidSelectPayload(const std::vector<std::uint8_t>& payload) {
    return payload.size() == 2 &&
        ((payload[0] == static_cast<std::uint8_t>(SelectKind::Map) && ValidMap(payload[1])) ||
         (payload[0] == static_cast<std::uint8_t>(SelectKind::Color) && ValidColor(payload[1])));
}

inline std::vector<std::uint8_t> HelloPayload() {
    return {static_cast<std::uint8_t>(kGameVersion >> 8),
            static_cast<std::uint8_t>(kGameVersion),
            static_cast<std::uint8_t>(kProtocolVersion >> 8),
            static_cast<std::uint8_t>(kProtocolVersion)};
}
inline std::uint16_t PayloadWord(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((std::uint16_t(p[0]) << 8) | p[1]);
}

// Room state: [count, P1 ready, P2 ready, map, P1 color, P2 color, host player ID].
inline bool ReadReadyPayload(const std::vector<std::uint8_t>& payload, bool& ready) {
    if (payload.size() != 1 || payload[0] > 1) return false;
    ready = payload[0] != 0;
    return true;
}
inline bool ValidRoomStatePayload(const std::vector<std::uint8_t>& payload) {
    return payload.size() == 7 && payload[0] <= 2 &&
           payload[1] <= 1 && payload[2] <= 1 && ValidMap(payload[3]) &&
           ValidColor(payload[4]) && ValidColor(payload[5]) &&
           (payload[0] == 0 ? payload[6] == 0 : payload[6] >= 1 && payload[6] <= 2);
}

constexpr std::uint8_t kInputNone = 0x00;
constexpr std::uint8_t kInputUp = 0x01;
constexpr std::uint8_t kInputDown = 0x02;
constexpr std::uint8_t kInputLeft = 0x04;
constexpr std::uint8_t kInputRight = 0x08;

struct InputData {
    std::uint8_t directionMask = kInputNone;
    std::uint32_t sequence = 0;
    bool placeBomb = false;
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

// Six wire bytes: direction, big-endian uint32 sequence, one-shot place flag.
// Player identity comes from the approved Session, never from this payload.
inline std::vector<std::uint8_t> InputPayload(std::uint8_t directionMask,
                                            std::uint32_t sequence, bool placeBomb = false) {
    if (!InputDirectionText(directionMask)) return {};
    return {directionMask, static_cast<std::uint8_t>(sequence >> 24),
            static_cast<std::uint8_t>(sequence >> 16),
            static_cast<std::uint8_t>(sequence >> 8), static_cast<std::uint8_t>(sequence),
            static_cast<std::uint8_t>(placeBomb)};
}

inline bool ReadInputPayload(const std::vector<std::uint8_t>& payload, InputData& output) {
    if (payload.size() != 6 || !InputDirectionText(payload[0]) || payload[5] > 1) return false;
    output.directionMask = payload[0];
    output.sequence = (std::uint32_t(PayloadWord(payload.data() + 1)) << 16) |
                      PayloadWord(payload.data() + 3);
    output.placeBomb = payload[5] != 0;
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
    case ErrorCode::SelectionDenied: return "Selection denied";
    default: return nullptr;
    }
}
} // namespace network
