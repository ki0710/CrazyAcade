#pragma once
#include <winsock2.h>
#include <Windows.h>
#include <conio.h>

namespace network {
// The console handler shares only this Interlocked-controlled flag.
inline volatile LONG& StopFlag() { static volatile LONG flag = 0; return flag; }
inline BOOL WINAPI StopHandler(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    InterlockedExchange(&StopFlag(), 1);
    return TRUE;
}
inline bool StopRequested() {
    if (InterlockedCompareExchange(&StopFlag(), 0, 0)) return true;
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const DWORD type = GetFileType(input);
    if (type == FILE_TYPE_CHAR) {
        while (_kbhit()) {
            const int ch = _getch();
            if (ch == '\r' || ch == '\n') return true;
        }
    } else if (type == FILE_TYPE_PIPE) {
        DWORD available = 0;
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) return true;
        while (available-- > 0) {
            char ch;
            DWORD read = 0;
            if (!ReadFile(input, &ch, 1, &read, nullptr) || !read) return true;
            if (ch == '\r' || ch == '\n') return true;
        }
    }
    return false;
}
} // namespace network
