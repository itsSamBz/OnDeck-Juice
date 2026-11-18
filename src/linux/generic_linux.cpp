// Linux fallbacks that work for any vendor:
//  * PollGenericHid: batteries the kernel exposes in /sys/class/power_supply for HID devices
//    (hid-input maps the standard HID Battery Strength usage, hid-logitech-dj / hid-playstation /
//    xpad / ... add their own). Only devices no vendor driver has claimed are reported.
//  * PollBluetooth: BlueZ over D-Bus (org.bluez.Battery1 = GATT Battery Service / HFP battery
//    indicator), plus the kernel's `hid-<address>-battery` supplies for Bluetooth HID devices.
//    Both are keyed by Bluetooth address so a device is only listed once.
#include <dirent.h>
#include <gio/gio.h>
#include <limits.h>
#include <stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

#include "../drivers/drivers.h"

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool LooksLikeMac(const std::string& s) {
    if (s.size() != 17) return false;
    for (size_t i = 0; i < s.size(); i++) {
        if (i % 3 == 2 ? s[i] != ':' : !isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

struct Supply {
    std::string id;        // directory name in /sys/class/power_supply
    std::string model;
    std::string mac;       // lowercase, only for Bluetooth HID supplies
    int percent = -1;
    bool charging = false;
    unsigned vid = 0, pid = 0;
};

std::vector<Supply> ReadSupplies() {
    std::vector<Supply> out;
    DIR* dir = opendir("/sys/class/power_supply");
    if (!dir) return out;
    std::vector<std::string> names;
    while (dirent* e = readdir(dir))
        if (e->d_name[0] != '.') names.push_back(e->d_name);
    closedir(dir);
    std::sort(names.begin(), names.end());

    for (const auto& n : names) {
        std::string base = "/sys/class/power_supply/" + n;
        if (ReadFile(base + "/type") != "Battery") continue;
        if (ReadFile(base + "/scope") != "Device") continue;  // the laptop's own battery has scope System
        std::string cap = ReadFile(base + "/capacity");
        if (cap.empty()) continue;
        Supply s;
        s.id = n;
        s.percent = Clamp100(atoi(cap.c_str()));
        std::string status = ReadFile(base + "/status");
        s.charging = status == "Charging";
        s.model = ReadFile(base + "/model_name");
        if (n.rfind("hid-", 0) == 0 && n.size() > 12 && n.compare(n.size() - 8, 8, "-battery") == 0) {
            std::string mid = n.substr(4, n.size() - 12);
            if (LooksLikeMac(mid)) s.mac = Lower(mid);
        }
        char real[PATH_MAX];
        if (realpath((base + "/device").c_str(), real)) {
            std::string d = real;
            std::string leaf = d.substr(d.find_last_of('/') + 1);  // 0003:1D57:FA60.0001
            unsigned bus = 0;
            sscanf(leaf.c_str(), "%x:%x:%x", &bus, &s.vid, &s.pid);
        }
        out.push_back(std::move(s));
    }
    return out;
}

DeviceKind KindFromClassOfDevice(uint32_t cod) {
    uint32_t major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
    if (major == 0x04) return DeviceKind::Headset;
    if (major == 0x05) {
        if ((minor & 0x0F) == 0x01 || (minor & 0x0F) == 0x02) return DeviceKind::Gamepad;
        if ((minor >> 4) == 1) return DeviceKind::Keyboard;
        if ((minor >> 4) == 2) return DeviceKind::Mouse;
    }
    return DeviceKind::Other;
}

DeviceKind KindFromIcon(const std::string& icon) {
    if (icon == "input-mouse") return DeviceKind::Mouse;
    if (icon == "input-keyboard") return DeviceKind::Keyboard;
    if (icon == "input-gaming") return DeviceKind::Gamepad;
    if (icon == "audio-headset" || icon == "audio-headphones") return DeviceKind::Headset;
    return DeviceKind::Other;
}

DeviceKind KindFromAppearance(unsigned appearance) {
    if ((appearance >> 6) != 0x0F) return DeviceKind::Other;  // HID category
    switch (appearance & 0x3F) {
        case 1: return DeviceKind::Keyboard;
        case 2: return DeviceKind::Mouse;
        case 3: case 4: return DeviceKind::Gamepad;
        default: return DeviceKind::Other;
    }
}

struct BtDevice {
    std::string mac, name;  // mac: lowercase
    DeviceKind kind = DeviceKind::Other;
    bool connected = false;
    int percent = -1;
    bool charging = false;
};

// All BlueZ devices (with battery percentage when BlueZ has one). Empty if BlueZ is not running.
std::map<std::string, BtDevice> ReadBluez() {
    std::map<std::string, BtDevice> out;
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
    if (!bus) { g_clear_error(&err); return out; }
    GVariant* res = g_dbus_connection_call_sync(bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", nullptr,
                                                G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
    if (!res) { g_clear_error(&err); return out; }
    GVariant* objs = g_variant_get_child_value(res, 0);
    GVariantIter it;
    g_variant_iter_init(&it, objs);
    const gchar* path;
    GVariant* ifaces;
    while (g_variant_iter_loop(&it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant* dev = g_variant_lookup_value(ifaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
        if (!dev) continue;
        BtDevice d;
        const gchar* s = nullptr;
        if (g_variant_lookup(dev, "Address", "&s", &s) && s) d.mac = Lower(s);
        if (g_variant_lookup(dev, "Alias", "&s", &s) && s) d.name = s;
        else if (g_variant_lookup(dev, "Name", "&s", &s) && s) d.name = s;
        gboolean connected = FALSE;
        if (g_variant_lookup(dev, "Connected", "b", &connected)) d.connected = connected;
        d.kind = DeviceKind::Other;
        if (g_variant_lookup(dev, "Icon", "&s", &s) && s) d.kind = KindFromIcon(s);
        guint16 appearance = 0;
        if (d.kind == DeviceKind::Other && g_variant_lookup(dev, "Appearance", "q", &appearance)) d.kind = KindFromAppearance(appearance);
        guint32 cls = 0;
        if (d.kind == DeviceKind::Other && g_variant_lookup(dev, "Class", "u", &cls)) d.kind = KindFromClassOfDevice(cls);
        g_variant_unref(dev);
        if (GVariant* bat = g_variant_lookup_value(ifaces, "org.bluez.Battery1", G_VARIANT_TYPE("a{sv}"))) {
            guchar pct = 0;
            if (g_variant_lookup(bat, "Percentage", "y", &pct) && pct <= 100) d.percent = pct;
            g_variant_unref(bat);
        }
        if (!d.mac.empty()) out[d.mac] = d;
    }
    g_variant_unref(objs);
    g_variant_unref(res);
    return out;
}

}  // namespace

void PollGenericHid(PollContext& ctx) {
    for (const Supply& s : ReadSupplies()) {
        if (!s.mac.empty()) continue;  // Bluetooth: PollBluetooth
        if (s.vid && ctx.IsClaimed((USHORT)s.vid, (USHORT)s.pid)) continue;
        DeviceStatus d;
        d.key = L"ps:" + Utf8ToWide(s.id);
        d.name = Utf8ToWide(s.model);
        if (d.name.empty()) d.name = L"Wireless device";
        d.via = L"HID";
        d.kind = s.vid ? KindFromCollections(ctx.colls, (USHORT)s.vid, (USHORT)s.pid) : DeviceKind::Other;
        if (d.kind == DeviceKind::Other) d.kind = GuessKind(d.name);
        d.percent = s.percent;
        d.charging = s.charging;
        if (s.vid) ctx.Add(d, (USHORT)s.vid, (USHORT)s.pid);
        else ctx.out.push_back(d);
    }
}

void PollBluetooth(PollContext& ctx) {
    std::map<std::string, BtDevice> devices = ReadBluez();

    // Bluetooth HID devices whose battery only the kernel knows about (BlueZ has no Battery1 for them).
    for (const Supply& s : ReadSupplies()) {
        if (s.mac.empty()) continue;
        BtDevice& d = devices[s.mac];
        if (d.mac.empty()) {
            d.mac = s.mac;
            d.name = s.model;
            d.connected = true;  // the supply only exists while the device is connected
        }
        d.percent = s.percent;  // the kernel reading is the most direct one
        d.charging = s.charging;
        if (d.name.empty()) d.name = s.model;
    }

    for (auto& [mac, d] : devices) {
        if (!d.connected || d.percent < 0) continue;
        DeviceStatus s;
        s.key = L"bt:" + Utf8ToWide(mac);
        s.name = Utf8ToWide(d.name);
        if (s.name.empty()) s.name = L"Bluetooth device";
        // Skip devices a vendor driver already reported over HID++ / Bragi on the same link.
        bool dup = false;
        for (auto& o : ctx.out) dup |= o.name == s.name;
        if (dup) continue;
        s.via = L"Bluetooth";
        s.percent = d.percent;
        s.charging = d.charging;
        s.kind = d.kind != DeviceKind::Other ? d.kind : GuessKind(s.name);
        ctx.out.push_back(s);
    }
}
