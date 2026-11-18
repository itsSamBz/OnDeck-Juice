#include "hid.h"

#include <setupapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>

std::vector<HidColl> EnumHid() {
    std::vector<HidColl> out;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO di = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &hidGuid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<BYTE> buf(need);
        auto det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(di, &ifd, det, need, nullptr, nullptr)) continue;
        HANDLE h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        HidColl c;
        c.path = det->DevicePath;
        HIDD_ATTRIBUTES a{ sizeof(a) };
        if (HidD_GetAttributes(h, &a)) { c.vid = a.VendorID; c.pid = a.ProductID; }
        PHIDP_PREPARSED_DATA pp;
        if (HidD_GetPreparsedData(h, &pp)) { HidP_GetCaps(pp, &c.caps); HidD_FreePreparsedData(pp); }
        CloseHandle(h);
        std::wstring lower = c.path;
        for (auto& ch : lower) ch = towlower(ch);
        size_t mi = lower.find(L"&mi_");
        if (mi != std::wstring::npos) c.iface = (int)wcstol(lower.c_str() + mi + 4, nullptr, 16);
        out.push_back(std::move(c));
    }
    SetupDiDestroyDeviceInfoList(di);
    return out;
}

// ---- HidDev -----------------------------------------------------------------------------------

bool HidDev::Open(const HidColl& c) {
    Close();
    c_ = c;
    h_ = CreateFileW(c.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                     FILE_FLAG_OVERLAPPED, nullptr);
    overlapped_ = true;
    return ok();
}

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    Close();
    c_ = c;
    h_ = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    overlapped_ = false;
    return ok();
}


void HidDev::Close() {
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    h_ = INVALID_HANDLE_VALUE;
}

bool HidDev::Write(const std::vector<BYTE>& data, int timeoutMs) {
    if (!ok() || !overlapped_ || !c_.outLen() || data.empty()) return false;
    std::vector<BYTE> buf(c_.outLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD n = 0;
    bool done = false;
    if (WriteFile(h_, buf.data(), (DWORD)buf.size(), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, (DWORD)timeoutMs) == WAIT_OBJECT_0) {
            done = GetOverlappedResult(h_, &ov, &n, FALSE) != FALSE;
        } else {
            CancelIo(h_);
            done = GetOverlappedResult(h_, &ov, &n, TRUE) != FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    return done;
}

int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    if (!ok() || !overlapped_ || !c_.inLen()) return -1;
    std::vector<BYTE> tmp(c_.inLen(), 0);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD got = 0;
    bool done = false;
    if (ReadFile(h_, tmp.data(), (DWORD)tmp.size(), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING) {
        if (timeoutMs > 0 && WaitForSingleObject(ov.hEvent, (DWORD)timeoutMs) == WAIT_OBJECT_0) {
            done = GetOverlappedResult(h_, &ov, &got, FALSE) != FALSE;
        } else {
            CancelIo(h_);
            done = GetOverlappedResult(h_, &ov, &got, TRUE) != FALSE;  // it may have completed meanwhile
        }
    }
    CloseHandle(ov.hEvent);
    if (!done || got == 0) return -1;
    size_t off = tmp[0] == 0 ? 1 : 0;  // hidapi semantics: drop the "no report ID" byte
    buf.assign(tmp.begin() + off, tmp.begin() + got);
    return (int)buf.size();
}

void HidDev::Flush() {
    if (ok()) HidD_FlushQueue(h_);
}

bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    if (!ok() || !c_.featLen()) return false;
    std::vector<BYTE> buf(c_.featLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    return HidD_SetFeature(h_, buf.data(), (ULONG)buf.size()) != FALSE;
}

bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    if (!ok() || !c_.featLen()) return false;
    out.assign(c_.featLen(), 0);
    out[0] = reportId;
    return HidD_GetFeature(h_, out.data(), (ULONG)out.size()) != FALSE;
}

// ---- helpers ----------------------------------------------------------------------------------

std::wstring HidProduct(const HidColl& c) {
    HANDLE h = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wchar_t name[128] = {};
    if (!HidD_GetProductString(h, name, sizeof(name) - sizeof(wchar_t))) name[0] = 0;
    CloseHandle(h);
    std::wstring s = name;
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

// Hex4, Clamp100, MapTo100, Interpolate, GuessKind and KindFromCollections live in hid_common.cpp.
