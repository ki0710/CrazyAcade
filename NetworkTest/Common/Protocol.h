#pragma once
#include "Packet.h"

namespace network {
constexpr std::uint16_t kGameVersion = 1;
constexpr std::uint16_t kProtocolVersion = 2;
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
