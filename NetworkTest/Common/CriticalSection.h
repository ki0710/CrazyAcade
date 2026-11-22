#pragma once

// Keep Winsock 2 ahead of Windows.h, including in standalone consumers.
#include <winsock2.h>
#include <windows.h>
#include <mutex>
#include <system_error>

namespace network {
// Owns a Windows critical section. All users must finish before destruction.
// Noncopyable and nonmovable: Windows synchronization objects cannot be relocated.
class CriticalSection {
public:
    CriticalSection() {
        if (!InitializeCriticalSectionAndSpinCount(&section_, 0)) {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "InitializeCriticalSectionAndSpinCount");
        }
    }

    ~CriticalSection() noexcept { DeleteCriticalSection(&section_); }

    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
    CriticalSection(CriticalSection&&) = delete;
    CriticalSection& operator=(CriticalSection&&) = delete;

    void lock() noexcept { EnterCriticalSection(&section_); }
    void unlock() noexcept { LeaveCriticalSection(&section_); }

private:
    CRITICAL_SECTION section_;
};

// Releases the lock on normal scope exit and during exception unwinding.
using CriticalSectionLock = std::lock_guard<CriticalSection>;
} // namespace network
