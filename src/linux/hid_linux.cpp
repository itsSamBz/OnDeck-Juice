// Linux HID backend on top of hidraw.
//
// Enumeration only reads sysfs (/sys/class/hidraw/hidrawN/device/{uevent,report_descriptor}), so it
// works without any device permissions. The report descriptor is parsed so that every top-level
// collection gets the same usage page / usage / report lengths Windows' HidP_GetCaps would report,
// which is what the vendor drivers were written against. Opening a device needs access to
// /dev/hidrawN (see packaging/70-wireless-battery.rules).
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "../hid.h"

// ---- UTF-8 helpers ----------------------------------------------------------------------------

std::wstring Utf8ToWide(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        uint32_t cp = 0xFFFD;
        int extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        i++;
        for (int k = 0; k < extra; k++, i++) {
            if (i >= s.size() || (s[i] & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | (s[i] & 0x3F);
        }
        out.push_back((wchar_t)cp);
    }
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    std::string out;
    for (wchar_t w : s) {
        uint32_t cp = (uint32_t)w;
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    return out;
}

namespace {

// ---- report descriptor parsing ----------------------------------------------------------------

struct CollInfo {
    USHORT page = 0, usage = 0;
    std::map<int, int> inBits, outBits, featBits;  // report ID -> payload bits
};

struct Descriptor {
    std::vector<CollInfo> colls;
    bool numbered = false;
};

int ByteLen(const std::map<int, int>& bits) {
    int best = 0;
    for (auto& [id, b] : bits) best = std::max(best, 1 + (b + 7) / 8);  // +1: the report ID byte, as on Windows
    return best;
}

Descriptor ParseDescriptor(const std::vector<BYTE>& d) {
    struct Global { uint32_t page = 0; int size = 0, count = 0, id = 0; };
    Descriptor out;
    Global g;
    std::vector<Global> stack;
    std::vector<uint32_t> usages;  // local state
    int depth = 0;
    CollInfo* cur = nullptr;
    out.colls.reserve(32);  // keep `cur` valid while collections are appended

    size_t i = 0;
    while (i < d.size()) {
        BYTE b = d[i++];
        if (b == 0xFE) {  // long item: [0xFE][len][tag][data...]
            if (i >= d.size()) break;
            i += 2 + d[i];
            continue;
        }
        int sz = b & 3;
        if (sz == 3) sz = 4;
        int type = (b >> 2) & 3, tag = b >> 4;
        if (i + sz > d.size()) break;
        uint32_t u = 0;
        for (int k = 0; k < sz; k++) u |= (uint32_t)d[i + k] << (8 * k);
        i += sz;

        if (type == 1) {  // global
            switch (tag) {
                case 0x0: g.page = u; break;
                case 0x7: g.size = (int)u; break;
                case 0x8: g.id = (int)u; if (g.id) out.numbered = true; break;
                case 0x9: g.count = (int)u; break;
                case 0xA: stack.push_back(g); break;
                case 0xB: if (!stack.empty()) { g = stack.back(); stack.pop_back(); } break;
            }
        } else if (type == 2) {  // local
            if (tag == 0x0) usages.push_back(sz == 4 ? u : ((g.page << 16) | (u & 0xFFFF)));
        } else if (type == 0) {  // main
            if (tag == 0xA) {  // collection
                if (depth == 0 && out.colls.size() < 32) {
                    CollInfo ci;
                    uint32_t first = usages.empty() ? (g.page << 16) : usages.front();
                    ci.page = (USHORT)(first >> 16);
                    ci.usage = (USHORT)(first & 0xFFFF);
                    out.colls.push_back(ci);
                    cur = &out.colls.back();
                }
                depth++;
            } else if (tag == 0xC) {
                if (depth > 0) depth--;
                if (depth == 0) cur = nullptr;
            } else if ((tag == 0x8 || tag == 0x9 || tag == 0xB) && cur) {
                int bits = g.size * g.count;
                auto& m = tag == 0x8 ? cur->inBits : tag == 0x9 ? cur->outBits : cur->featBits;
                m[g.id] += bits;
            }
            usages.clear();
        }
    }
    return out;
}

std::string ReadText(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::vector<BYTE> ReadBinary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<BYTE>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string RealPath(const std::string& p) {
    char buf[PATH_MAX];
    return realpath(p.c_str(), buf) ? std::string(buf) : std::string();
}

std::string Dirname(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? "" : p.substr(0, s);
}

}  // namespace

std::vector<HidColl> EnumHid() {
    std::vector<HidColl> out;
    std::vector<int> nodes;
    if (DIR* dir = opendir("/sys/class/hidraw")) {
        while (dirent* e = readdir(dir)) {
            int n = -1;
            if (sscanf(e->d_name, "hidraw%d", &n) == 1) nodes.push_back(n);
        }
        closedir(dir);
    }
    std::sort(nodes.begin(), nodes.end());

    for (int n : nodes) {
        std::string base = "/sys/class/hidraw/hidraw" + std::to_string(n);
        std::string dev = RealPath(base + "/device");
        if (dev.empty()) continue;

        unsigned bus = 0, vid = 0, pid = 0;
        std::string name;
        std::istringstream uevent(ReadText(dev + "/uevent"));
        for (std::string line; std::getline(uevent, line);) {
            if (line.rfind("HID_ID=", 0) == 0) sscanf(line.c_str() + 7, "%x:%x:%x", &bus, &vid, &pid);
            else if (line.rfind("HID_NAME=", 0) == 0) name = line.substr(9);
        }
        if (!vid && !pid) continue;

        HidColl base_c;
        base_c.path = Utf8ToWide("/dev/hidraw" + std::to_string(n));
        base_c.vid = (USHORT)vid;
        base_c.pid = (USHORT)pid;
        base_c.product = Utf8ToWide(name);
        base_c.bluetooth = bus == 0x0005;
        if (bus == 0x0003) {  // USB: the parent of the HID device is the USB interface
            std::string ifdir = Dirname(dev);
            std::string num = ReadText(ifdir + "/bInterfaceNumber");
            if (!num.empty()) base_c.iface = (int)strtol(num.c_str(), nullptr, 16);
            std::string prod = ReadText(Dirname(ifdir) + "/product");
            if (!prod.empty()) base_c.product = Utf8ToWide(prod);
        }

        Descriptor desc = ParseDescriptor(ReadBinary(dev + "/report_descriptor"));
        int colNo = 0;
        for (const CollInfo& ci : desc.colls) {
            HidColl c = base_c;
            colNo++;
            // Like Windows' "&colNN" suffix: unique per collection when an interface has several.
            if (desc.colls.size() > 1) {
                char tag[16];
                snprintf(tag, sizeof(tag), "#col%02d", colNo);
                c.path += Utf8ToWide(tag);
            }
            c.caps.UsagePage = ci.page;
            c.caps.Usage = ci.usage;
            c.caps.InputReportByteLength = (USHORT)ByteLen(ci.inBits);
            c.caps.OutputReportByteLength = (USHORT)ByteLen(ci.outBits);
            c.caps.FeatureReportByteLength = (USHORT)ByteLen(ci.featBits);
            c.numbered = desc.numbered;
            for (auto& [id, bits] : ci.inBits) c.inIds.push_back((BYTE)id);
            out.push_back(std::move(c));
        }
    }
    return out;
}

// ---- HidDev -----------------------------------------------------------------------------------


bool HidDev::Open(const HidColl& c) {
    Close();
    c_ = c;
    std::string path = WideToUtf8(c.path);
    path = path.substr(0, path.find('#'));  // strip the collection tag
    fd_ = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    writable_ = fd_ >= 0;
    if (fd_ < 0) fd_ = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);  // enough for passive listeners
    return ok();
}

bool HidDev::OpenFeatureOnly(const HidColl& c) { return Open(c); }

void HidDev::Close() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
    writable_ = false;
}

bool HidDev::Write(const std::vector<BYTE>& data, int timeoutMs) {
    if (!ok() || !writable_ || !c_.outLen() || data.empty()) return false;
    std::vector<BYTE> buf(c_.outLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    ULONGLONG end = GetTickCount64() + (timeoutMs > 0 ? timeoutMs : 0);
    for (;;) {
        ssize_t n = write(fd_, buf.data(), buf.size());
        if (n >= 0) return true;
        if (errno == EINTR) continue;
        if (errno != EAGAIN) return false;
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return false;
        pollfd p{ fd_, POLLOUT, 0 };
        poll(&p, 1, left);
    }
}

int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    if (!ok() || !c_.inLen()) return -1;
    ULONGLONG end = GetTickCount64() + (timeoutMs > 0 ? timeoutMs : 0);
    std::vector<BYTE> tmp(4096);
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left < 0) left = 0;
        pollfd p{ fd_, POLLIN, 0 };
        int r = poll(&p, 1, left);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        ssize_t n = read(fd_, tmp.data(), tmp.size());
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return -1;  // e.g. ENODEV after unplugging
        }
        if (n == 0) return -1;
        // A hidraw node delivers every report of its interface; keep only this collection's.
        if (c_.numbered && std::find(c_.inIds.begin(), c_.inIds.end(), tmp[0]) == c_.inIds.end()) continue;
        buf.assign(tmp.begin(), tmp.begin() + n);
        return (int)buf.size();
    }
}

void HidDev::Flush() {
    if (!ok()) return;
    std::vector<BYTE> tmp(4096);
    for (;;) {
        pollfd p{ fd_, POLLIN, 0 };
        if (poll(&p, 1, 0) <= 0 || read(fd_, tmp.data(), tmp.size()) <= 0) break;
    }
}

bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    if (!ok() || !c_.featLen() || data.empty()) return false;
    std::vector<BYTE> buf(c_.featLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    return ioctl(fd_, HIDIOCSFEATURE(buf.size()), buf.data()) >= 0;
}

bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    if (!ok() || !c_.featLen()) return false;
    out.assign(c_.featLen(), 0);
    out[0] = reportId;
    return ioctl(fd_, HIDIOCGFEATURE(out.size()), out.data()) >= 0;
}

std::wstring HidProduct(const HidColl& c) {
    std::wstring s = c.product;
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}
