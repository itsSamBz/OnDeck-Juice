// Wireless Battery for Linux: a small daemon that polls every supported device and shows the battery
// levels in the panel / taskbar as a StatusNotifierItem (see tray.h).
//
//   wireless-battery            run the tray indicator
//   wireless-battery --list     print every device and its battery level, then exit
//   wireless-battery --dump     print every HID collection (for adding a new device), then exit
//   wireless-battery --quit     close the running instance
#include <glib-unix.h>
#include <gio/gio.h>
#include <libudev.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include "../devices.h"
#include "../hid.h"
#include "tray.h"

namespace {

constexpr int kPollIntervalSec = 60;
constexpr int kLowBattery = 15;

// Vendors whose hidraw nodes packaging/70-wireless-battery.rules opens up to the logged-in user
// (keep in sync with that file); used to explain a missing-permission situation.
const unsigned kUdevVendors[] = {
    0x03f0, 0x0951, 0x046d, 0x04a5, 0x054c, 0x0b05, 0x0bda, 0x1038, 0x1532, 0x1b1c, 0x1d57, 0x248a, 0x249a, 0x24ae, 0x2516,
    0x258a, 0x33e4, 0x3434, 0x3537, 0x3554, 0x3710, 0x373b, 0x373e, 0x3770, 0x37b0, 0x3151, 0x0db0, 0x145f, 0x14a5, 0x25aa,
    0x342d, 0x347a, 0x374a, 0x379a, 0x38a9, 0x38ee, 0x391d,
};

// ---- state (main thread only, except the worker bits) -----------------------------------------

struct Entry {
    DeviceStatus st;
    int lastPct = -1;
    ULONGLONG lastSeen = 0;
    bool lowWarned = false;
};

std::map<std::wstring, Entry> g_entries;
std::set<std::string> g_hidden;
GMainLoop* g_loop = nullptr;
guint g_soonId = 0;

std::mutex g_mu;
std::condition_variable g_cv;
bool g_refreshReq = false, g_stop = false;
std::thread g_worker;
int g_lockFd = -1;

// ---- small helpers ----------------------------------------------------------------------------

void Out(const std::wstring& s) {
    std::string u = WideToUtf8(s);
    fwrite(u.data(), 1, u.size(), stdout);
}

std::string ConfigDir() { return std::string(g_get_user_config_dir()) + "/wireless-battery"; }
std::string HiddenFile() { return ConfigDir() + "/hidden.txt"; }
std::string AutostartFile() { return std::string(g_get_user_config_dir()) + "/autostart/wireless-battery.desktop"; }

std::string ExePath() {
    if (const char* ai = getenv("APPIMAGE"))  // inside an AppImage /proc/self/exe is a temporary mount
        if (*ai) return ai;
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return n > 0 ? std::string(buf, n) : std::string("wireless-battery");
}

void LoadHidden() {
    std::ifstream f(HiddenFile());
    for (std::string line; std::getline(f, line);)
        if (!line.empty()) g_hidden.insert(line);
}

void SaveHidden() {
    g_mkdir_with_parents(ConfigDir().c_str(), 0700);
    std::ofstream f(HiddenFile());
    for (auto& k : g_hidden) f << k << "\n";
}

bool AutostartEnabled() { return g_file_test(AutostartFile().c_str(), G_FILE_TEST_EXISTS); }

void SetAutostart(bool on) {
    std::string path = AutostartFile();
    if (!on) {
        unlink(path.c_str());
        return;
    }
    g_mkdir_with_parents((std::string(g_get_user_config_dir()) + "/autostart").c_str(), 0700);
    std::ofstream f(path);
    f << "[Desktop Entry]\nType=Application\nName=Wireless Battery\n"
         "Comment=Battery levels of wireless mice, headsets, keyboards and controllers\n"
         "Exec=\"" << ExePath() << "\"\nIcon=battery\nTerminal=false\nX-GNOME-Autostart-enabled=true\nX-GNOME-Autostart-Delay=5\n";
}

int KindOrder(DeviceKind k) {  // headset first, then mouse, ... (same order as the Windows widget)
    switch (k) {
        case DeviceKind::Headset:  return 0;
        case DeviceKind::Mouse:    return 1;
        case DeviceKind::Keyboard: return 2;
        case DeviceKind::Gamepad:  return 3;
        default:                   return 4;
    }
}

const wchar_t* Glyph(DeviceKind k) {
    switch (k) {
        case DeviceKind::Headset:  return L"\U0001F3A7";  // headphone
        case DeviceKind::Mouse:    return L"\U0001F5B1";  // computer mouse
        case DeviceKind::Keyboard: return L"\u2328";      // keyboard
        case DeviceKind::Gamepad:  return L"\U0001F3AE";  // video game
        default:                   return L"\U0001F50B";  // battery
    }
}

std::wstring StatusText(const Entry& e) {
    const auto& s = e.st;
    if (!s.connected) return L"off / not connected";
    std::wstring t;
    if (!s.levelText.empty() && s.percent >= 0) t = s.levelText;
    else if (s.percent >= 0) t = std::to_wstring(s.percent) + L"%";
    else if (e.lastPct >= 0) t = std::to_wstring(e.lastPct) + L"% (last known)";
    else t = L"unknown";
    if (s.charging) t += L", charging";
    if (s.asleep) t += L", asleep";
    return t;
}

// The panel icon is the device's own icon (no battery icon); the percentage is shown as text.
std::string IconFor(DeviceKind k) {
    switch (k) {
        case DeviceKind::Headset:  return "audio-headset-symbolic";
        case DeviceKind::Mouse:    return "input-mouse-symbolic";
        case DeviceKind::Keyboard: return "input-keyboard-symbolic";
        case DeviceKind::Gamepad:  return "input-gaming-symbolic";
        default:                   return "battery-symbolic";
    }
}

// ---- display ----------------------------------------------------------------------------------

// GNOME's AppIndicator extension draws the text label; most other hosts (KDE, XFCE, ...) ignore it.
bool HostShowsLabel() {
    if (const char* o = getenv("WIRELESS_BATTERY_TEXT_ICON")) return *o == '0';
    const char* d = getenv("XDG_CURRENT_DESKTOP");
    std::string desk = d ? d : "";
    for (auto& c : desk) c = (char)toupper((unsigned char)c);
    return desk.find("GNOME") != std::string::npos || desk.find("UNITY") != std::string::npos ||
           desk.find("ZORIN") != std::string::npos || desk.find("PANTHEON") != std::string::npos;
}

void UpdateUi() {
    std::vector<const Entry*> all;
    for (auto& [k, e] : g_entries)
        if (e.st.connected) all.push_back(&e);
    std::sort(all.begin(), all.end(), [](const Entry* a, const Entry* b) {
        int ka = KindOrder(a->st.kind), kb = KindOrder(b->st.kind);
        return ka != kb ? ka < kb : a->st.name < b->st.name;
    });

    tray::State s;
    std::wstring label, tip;
    std::string iconName = "battery-missing-symbolic";
    bool first = true;
    int lowestPct = -1;
    bool lowestCharging = false;
    for (const Entry* e : all) {
        std::string key = WideToUtf8(e->st.key);
        bool shown = !g_hidden.count(key);
        int pct = e->st.percent >= 0 ? e->st.percent : e->lastPct;
        s.devices.push_back({ key, WideToUtf8(std::wstring(Glyph(e->st.kind)) + L" " + e->st.name + L": " + StatusText(*e) + L"  (" + e->st.via + L")"), shown });
        if (!shown) continue;

        std::wstring value = (!e->st.levelText.empty() && e->st.percent >= 0) ? e->st.levelText : pct >= 0 ? std::to_wstring(pct) + L"%" : L"?";
        if (e->st.charging) value += L"\u26A1";
        if (e->st.percent < 0) value += L"\U0001F4A4";  // asleep: showing the last known value
        if (pct >= 0 && (lowestPct < 0 || pct < lowestPct)) {
            lowestPct = pct;
            lowestCharging = e->st.charging;
        }
        if (first) {
            iconName = IconFor(e->st.kind);  // the icon itself shows the device type
            label = value;
            first = false;
        } else {
            label += L"  " + std::wstring(Glyph(e->st.kind)) + L" " + value;
        }
        if (!tip.empty()) tip += L"\n";
        tip += e->st.name + L": " + StatusText(*e);
    }
    if (tip.empty()) tip = L"No devices with battery info found";
    s.label = WideToUtf8(label);
    s.tooltip = WideToUtf8(tip);
    s.iconName = iconName;
    if (!HostShowsLabel()) {
        s.usePixmap = true;
        s.label.clear();
        s.pixText = lowestPct < 0 ? "?" : std::to_string(std::min(lowestPct, 99));
        s.pixCharging = lowestCharging;
        s.pixLow = lowestPct >= 0 && lowestPct <= kLowBattery && !lowestCharging;
    }
    s.autostart = AutostartEnabled();
    tray::Update(s);
}

void CheckLowBattery(Entry& e) {
    const auto& s = e.st;
    int shown = s.percent >= 0 ? s.percent : e.lastPct;
    if (s.charging || shown > kLowBattery + 10) e.lowWarned = false;
    if (!s.charging && s.percent >= 0 && s.percent <= kLowBattery && !e.lowWarned) {
        e.lowWarned = true;
        tray::Notify("Low battery", WideToUtf8(s.name + L" is at " + std::to_wstring(s.percent) + L"%"));
    }
}

void OnResults(std::vector<DeviceStatus>& results) {
    std::set<std::wstring> seen;
    ULONGLONG now = GetTickCount64();
    for (auto& s : results) {
        auto& e = g_entries[s.key];
        e.st = s;
        if (s.percent >= 0) e.lastPct = s.percent;
        e.lastSeen = now;
        seen.insert(s.key);
        CheckLowBattery(e);
    }
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        auto& e = it->second;
        // Some receivers drop sleeping devices from the link; keep showing the last value for a while.
        bool inGrace = !seen.count(it->first) && e.st.graceSec > 0 && e.st.connected && now - e.lastSeen < (ULONGLONG)e.st.graceSec * 1000;
        if (inGrace) {
            e.st.percent = -1;
            e.st.asleep = true;
        }
        if (!seen.count(it->first) && !inGrace) it = g_entries.erase(it);
        else ++it;
    }
    UpdateUi();
}

// ---- polling thread ---------------------------------------------------------------------------

void RefreshNow() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_refreshReq = true;
    g_cv.notify_one();
}

void RefreshSoon(guint ms) {
    if (g_soonId) return;  // debounce bursts of udev / BlueZ events
    g_soonId = g_timeout_add(ms, [](gpointer) -> gboolean {
        g_soonId = 0;
        RefreshNow();
        return G_SOURCE_REMOVE;
    }, nullptr);
}

void Worker() {
    std::unique_lock<std::mutex> lk(g_mu);
    while (!g_stop) {
        lk.unlock();
        auto* res = new std::vector<DeviceStatus>(PollAllDevices());
        g_main_context_invoke(nullptr, [](gpointer p) -> gboolean {
            std::unique_ptr<std::vector<DeviceStatus>> r(static_cast<std::vector<DeviceStatus>*>(p));
            OnResults(*r);
            return G_SOURCE_REMOVE;
        }, res);
        lk.lock();
        g_cv.wait_for(lk, std::chrono::seconds(kPollIntervalSec), [] { return g_refreshReq || g_stop; });
        g_refreshReq = false;
    }
}

// ---- event sources that trigger a refresh -----------------------------------------------------

gboolean OnUdev(GIOChannel*, GIOCondition, gpointer data) {
    auto* mon = static_cast<udev_monitor*>(data);
    while (udev_device* dev = udev_monitor_receive_device(mon)) {
        const char* action = udev_device_get_action(dev);
        if (action && (!strcmp(action, "add") || !strcmp(action, "remove"))) RefreshSoon(1500);
        udev_device_unref(dev);
    }
    return G_SOURCE_CONTINUE;
}

void WatchUdev() {
    udev* u = udev_new();
    if (!u) return;
    udev_monitor* mon = udev_monitor_new_from_netlink(u, "udev");
    if (!mon) return;
    udev_monitor_filter_add_match_subsystem_devtype(mon, "hidraw", nullptr);
    udev_monitor_filter_add_match_subsystem_devtype(mon, "power_supply", nullptr);
    udev_monitor_filter_add_match_subsystem_devtype(mon, "tty", nullptr);
    if (udev_monitor_enable_receiving(mon) < 0) return;
    GIOChannel* ch = g_io_channel_unix_new(udev_monitor_get_fd(mon));
    g_io_add_watch(ch, G_IO_IN, OnUdev, mon);
}

void WatchDBus() {
    GError* err = nullptr;
    if (GDBusConnection* sys = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err)) {
        auto cb = [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*, gpointer) { RefreshSoon(2000); };
        for (const char* arg0 : { "org.bluez.Battery1", "org.bluez.Device1" })
            g_dbus_connection_signal_subscribe(sys, "org.bluez", "org.freedesktop.DBus.Properties", "PropertiesChanged", nullptr, arg0,
                                               G_DBUS_SIGNAL_FLAGS_NONE, cb, nullptr, nullptr);
        g_dbus_connection_signal_subscribe(sys, "org.bluez", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", nullptr, nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE, cb, nullptr, nullptr);
        g_dbus_connection_signal_subscribe(sys, "org.freedesktop.login1", "org.freedesktop.login1.Manager", "PrepareForSleep", nullptr, nullptr,
                                           G_DBUS_SIGNAL_FLAGS_NONE,
                                           [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* p, gpointer) {
                                               gboolean sleeping = TRUE;
                                               g_variant_get(p, "(b)", &sleeping);
                                               if (!sleeping) RefreshSoon(3000);  // resumed
                                           },
                                           nullptr, nullptr);
    }
    g_clear_error(&err);
}

// ---- single instance --------------------------------------------------------------------------

std::string LockPath() { return std::string(g_get_user_runtime_dir()) + "/wireless-battery.pid"; }

bool AcquireLock() {
    g_lockFd = open(LockPath().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (g_lockFd < 0) return true;  // cannot lock: run anyway
    if (flock(g_lockFd, LOCK_EX | LOCK_NB) != 0) return false;
    if (ftruncate(g_lockFd, 0) == 0) {
        std::string pid = std::to_string(getpid()) + "\n";
        if (write(g_lockFd, pid.data(), pid.size()) < 0) { /* best effort */ }
    }
    return true;
}

int QuitRunning() {
    std::ifstream f(LockPath());
    int pid = 0;
    f >> pid;
    if (pid > 1 && kill(pid, SIGTERM) == 0) {
        for (int i = 0; i < 50 && kill(pid, 0) == 0; i++) usleep(100000);
    }
    return 0;
}

// ---- command line -----------------------------------------------------------------------------

void PermissionHint() {
    std::set<std::string> blocked;
    for (const auto& c : EnumHid()) {
        if (std::find(std::begin(kUdevVendors), std::end(kUdevVendors), (unsigned)c.vid) == std::end(kUdevVendors)) continue;
        std::string node = WideToUtf8(c.path);
        node = node.substr(0, node.find('#'));
        if (access(node.c_str(), R_OK | W_OK) != 0) blocked.insert(node);
    }
    if (blocked.empty()) return;
    fprintf(stderr, "\nNote: %zu HID device node(s) of supported vendors are not accessible to your user (e.g. %s).\n"
                    "Wireless dongles can only be read once the udev rule is installed:\n"
                    "    sudo ./packaging/install.sh --udev-only\n"
                    "then unplug and re-plug the dongle (or log out and in).\n", blocked.size(), blocked.begin()->c_str());
}

int ListToConsole(bool dump) {
    if (dump) {
        Out(DumpHidCollections());
        return 0;
    }
    auto list = PollAllDevices();
    if (list.empty()) Out(L"No devices with battery info found.\n");
    for (auto& s : list) {
        std::wstring line = s.name + L"  [" + s.via + L"]  ";
        if (!s.connected) line += L"off / not connected";
        else if (s.asleep) line += L"asleep";
        else if (!s.levelText.empty()) line += s.levelText;
        else if (s.percent >= 0) line += std::to_wstring(s.percent) + L"%";
        else line += L"unknown";
        if (s.charging) line += L", charging";
        if (s.millivolts) line += L", " + std::to_wstring(s.millivolts) + L" mV";
        Out(line + L"   (" + s.key + L")\n");
    }
    fflush(stdout);
    PermissionHint();
    return 0;
}

void Usage() {
    puts("Usage: wireless-battery [--list | --dump | --quit | --help]\n"
         "  (no option)  run the panel indicator\n"
         "  --list       print every device and its battery level, then exit\n"
         "  --dump       print every HID collection (useful when asking for a new device)\n"
         "  --quit       close the running instance");
}

void Shutdown() {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_stop = true;
        g_cv.notify_one();
    }
    // The worker may be inside a long poll; it only touches the main loop through g_main_context_invoke.
    if (g_worker.joinable()) g_worker.detach();
    tray::Shutdown();
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) return ListToConsole(false);
        if (!strcmp(argv[i], "--dump")) return ListToConsole(true);
        if (!strcmp(argv[i], "--quit")) return QuitRunning();
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { Usage(); return 0; }
        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        Usage();
        return 2;
    }

    if (!AcquireLock()) {
        fprintf(stderr, "wireless-battery is already running (use --quit to close it).\n");
        return 0;
    }

    LoadHidden();
    tray::Callbacks cb;
    cb.toggleDevice = [](const std::string& key) {
        if (!g_hidden.erase(key)) g_hidden.insert(key);
        SaveHidden();
        UpdateUi();
    };
    cb.refresh = [] { RefreshNow(); };
    cb.setAutostart = [](bool on) {
        SetAutostart(on);
        UpdateUi();
    };
    cb.quit = [] { g_main_loop_quit(g_loop); };
    if (!tray::Init(cb)) return 1;

    g_loop = g_main_loop_new(nullptr, FALSE);
    auto quit = [](gpointer) -> gboolean { g_main_loop_quit(g_loop); return G_SOURCE_REMOVE; };
    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);
    g_unix_signal_add(SIGHUP, quit, nullptr);

    UpdateUi();  // something to click on until the first poll finishes
    WatchUdev();
    WatchDBus();
    g_worker = std::thread(Worker);

    g_main_loop_run(g_loop);
    Shutdown();
    // The detached worker may still be polling a device; skip static destructors so it cannot trip over them.
    fflush(nullptr);
    _exit(0);
}
