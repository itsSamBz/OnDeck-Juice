// ATK N9 Pro headset (VID 373B: dongle 11CB, wired 11CA) on Linux. Same protocol as the Windows
// driver (src/drivers/atk_serial.cpp), but over the cdc_acm tty (/dev/ttyACMn) instead of a COM port.
// Needs read/write access to the tty (group `dialout` on Ubuntu / Zorin OS).
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "../drivers/drivers.h"

namespace {

struct Port { std::string dev; USHORT pid; };

std::string ReadLine(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

std::vector<Port> FindPorts() {
    std::vector<Port> out;
    DIR* dir = opendir("/sys/class/tty");
    if (!dir) return out;
    while (dirent* e = readdir(dir)) {
        if (strncmp(e->d_name, "ttyACM", 6) != 0) continue;
        char real[PATH_MAX];
        if (!realpath((std::string("/sys/class/tty/") + e->d_name + "/device").c_str(), real)) continue;
        std::string ifdir = real;  // .../1-2:1.0
        std::string usbdir = ifdir.substr(0, ifdir.find_last_of('/'));
        unsigned vid = strtoul(ReadLine(usbdir + "/idVendor").c_str(), nullptr, 16);
        unsigned pid = strtoul(ReadLine(usbdir + "/idProduct").c_str(), nullptr, 16);
        if (vid == 0x373B && (pid == 0x11CA || pid == 0x11CB)) out.push_back({ std::string("/dev/") + e->d_name, (USHORT)pid });
    }
    closedir(dir);
    return out;
}

class Serial {
public:
    explicit Serial(const std::string& dev) {
        fd_ = open(dev.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) return;
        termios t{};
        if (tcgetattr(fd_, &t) != 0) { Close(); return; }
        cfmakeraw(&t);
        cfsetispeed(&t, B9600);
        cfsetospeed(&t, B9600);
        t.c_cflag |= CLOCAL | CREAD;
        if (tcsetattr(fd_, TCSANOW, &t) != 0) { Close(); return; }
        tcflush(fd_, TCIOFLUSH);
    }
    ~Serial() { Close(); }
    bool ok() const { return fd_ >= 0; }

    bool Command(BYTE cmd, BYTE sub, std::vector<BYTE>& data) {
        BYTE frame[64] = { 2, cmd, sub };
        if (write(fd_, frame, sizeof(frame)) != (ssize_t)sizeof(frame)) return false;
        std::vector<BYTE> buf;
        ULONGLONG end = GetTickCount64() + 800;
        while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            pollfd p{ fd_, POLLIN, 0 };
            if (poll(&p, 1, left) <= 0) continue;
            BYTE chunk[128];
            ssize_t n = read(fd_, chunk, sizeof(chunk));
            if (n < 0 && errno != EAGAIN && errno != EINTR) return false;
            if (n > 0) buf.insert(buf.end(), chunk, chunk + n);
            while (buf.size() >= 64) {  // the headset also pushes unsolicited frames; skip those
                if (buf[1] == cmd && buf[2] == sub) {
                    data.assign(buf.begin() + 3, buf.begin() + 64);
                    return true;
                }
                buf.erase(buf.begin(), buf.begin() + 64);
            }
        }
        return false;
    }

private:
    void Close() {
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
    }
    int fd_ = -1;
};

}  // namespace

void PollAtkSerial(PollContext& ctx) {
    for (const Port& p : FindPorts()) {
        if (ctx.IsClaimed(0x373B, p.pid)) continue;
        Serial port(p.dev);
        if (!port.ok()) continue;  // no permission, or in use
        std::vector<BYTE> data;
        if (!port.Command(0x00, 0x04, data)) continue;
        DeviceStatus s;
        s.key = L"atk-serial:" + Hex4(p.pid);
        s.name = L"ATK N9 Pro";
        s.kind = DeviceKind::Headset;
        s.via = p.pid == 0x11CA ? L"USB cable" : L"2.4 GHz receiver";
        s.connected = data[1] == 1;
        if (s.connected && port.Command(0x00, 0x03, data)) {
            s.percent = Clamp100(data[1] & 0x7F);
            s.charging = (data[1] & 0x80) != 0;
        }
        ctx.Add(s, 0x373B, p.pid);
    }
}

// No XInput / Windows.Gaming.Input on Linux: Xbox, DualSense etc. controllers show up through the
// kernel power_supply class (PollGenericHid) or BlueZ (PollBluetooth).
void PollXInput(PollContext&) {}
