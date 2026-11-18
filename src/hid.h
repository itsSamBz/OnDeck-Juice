#pragma once
// Thin HID layer shared by all drivers (Win32 HID API on Windows, hidraw on Linux).
//
// Report buffers follow hidapi conventions so protocol code from other projects maps 1:1:
//  * Write/SetFeature: data[0] is the report ID (0 when the device does not number its reports);
//    the buffer is zero-padded to the report length.
//  * Read: a leading 0 report ID is stripped, so data[0] is the first payload byte for
//    unnumbered reports and the report ID for numbered ones.
//
// A HidColl is one *top-level collection* (what Windows exposes as one HID device). On Linux several
// collections can share one hidraw node (one per USB interface); Read() then only returns the reports
// that belong to the collection that was opened.
#include "platform.h"

#ifdef _WIN32
#include <hidsdi.h>
#include <hidpi.h>
#else
// The subset of HIDP_CAPS the drivers use, filled from the report descriptor.
struct HIDP_CAPS {
    USHORT Usage = 0, UsagePage = 0;
    USHORT InputReportByteLength = 0, OutputReportByteLength = 0, FeatureReportByteLength = 0;
};
#endif

#include <initializer_list>
#include <string>
#include <vector>

#include "devices.h"

struct HidColl {
    std::wstring path;  // device node (Linux: /dev/hidrawN, shared by every collection of an interface)
    USHORT vid = 0, pid = 0;
    int iface = -1;  // USB interface number (MI_xx in the path), -1 when unknown
    HIDP_CAPS caps{};
#ifndef _WIN32
    std::wstring product;               // USB product string / HID name
    bool numbered = false;              // the node uses report IDs at all
    std::vector<BYTE> inIds;            // input report IDs belonging to this collection
    bool bluetooth = false;             // HID over Bluetooth (BR/EDR or GATT)
#endif
    USHORT page() const { return caps.UsagePage; }
    USHORT usage() const { return caps.Usage; }
    USHORT inLen() const { return caps.InputReportByteLength; }
    USHORT outLen() const { return caps.OutputReportByteLength; }
    USHORT featLen() const { return caps.FeatureReportByteLength; }
};

std::vector<HidColl> EnumHid();

class HidDev {
public:
    HidDev() = default;
    ~HidDev() { Close(); }
    HidDev(const HidDev&) = delete;
    HidDev& operator=(const HidDev&) = delete;

    bool Open(const HidColl& c);            // read/write
    bool OpenFeatureOnly(const HidColl& c);  // enough for feature reports (zero access on Windows)
    void Close();
#ifdef _WIN32
    bool ok() const { return h_ != INVALID_HANDLE_VALUE; }
#else
    bool ok() const { return fd_ >= 0; }
#endif
    const HidColl& coll() const { return c_; }

    bool Write(const std::vector<BYTE>& data, int timeoutMs = 500);
    bool Write(std::initializer_list<BYTE> data, int timeoutMs = 500) { return Write(std::vector<BYTE>(data), timeoutMs); }
    // Reads one input report into `buf` (resized to fit). Returns the byte count, or -1 on timeout/error.
    int Read(std::vector<BYTE>& buf, int timeoutMs);
    void Flush();  // discard queued input reports

    bool SetFeature(const std::vector<BYTE>& data);
    bool GetFeature(BYTE reportId, std::vector<BYTE>& out);  // out[0] = report ID

private:
#ifdef _WIN32
    HANDLE h_ = INVALID_HANDLE_VALUE;
    bool overlapped_ = false;
#else
    int fd_ = -1;
    bool writable_ = false;
#endif
    HidColl c_;
};

std::wstring HidProduct(const HidColl& c);
std::wstring Hex4(USHORT v);
int Clamp100(int v);
int MapTo100(int v, int lo, int hi);  // linear map lo..hi -> 0..100, clamped
// Piecewise-linear interpolation through (mv[i], pct[i]); tables sorted by descending voltage.
int Interpolate(int mv, const int* mvTable, const int* pctTable, int n);
DeviceKind GuessKind(const std::wstring& name);
// Device type from the top-level collections a device exposes (mouse > gamepad > keyboard).
DeviceKind KindFromCollections(const std::vector<HidColl>& all, USHORT vid, USHORT pid);
