#pragma once
// Minimal Windows-type shim so the vendor drivers (written against Win32 typedefs) build unchanged
// on Linux. On Windows this header just pulls in <windows.h>.
#ifdef _WIN32
#include <windows.h>
#else
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

typedef uint8_t BYTE;
typedef uint16_t USHORT;
typedef uint16_t WORD;
typedef uint16_t USAGE;
typedef uint32_t UINT;
typedef uint32_t UINT32;
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;

inline void Sleep(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline ULONGLONG GetTickCount64() {
    using namespace std::chrono;
    return (ULONGLONG)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// UTF-8 <-> wchar_t (UTF-32 on Linux). Independent of the process locale.
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);
#endif
