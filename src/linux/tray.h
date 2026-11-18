#pragma once
// Panel / taskbar presence on Linux through the StatusNotifierItem protocol (what GNOME's AppIndicator
// extension, Zorin Taskbar, KDE Plasma, Cinnamon, XFCE ... all implement). One item is shown with a
// themed battery icon plus a text label ("🎧 30%  🖱 93%") next to it, and a menu with every device.
#include <functional>
#include <string>
#include <vector>

namespace tray {

struct MenuDevice {
    std::string key;   // stable device identity, passed back to Callbacks::toggleDevice
    std::string text;  // "🖱 Attack Shark X11: 93%  (2.4 GHz receiver)"
    bool shown;        // checked = shown in the panel label
};

struct State {
    std::string label;     // text next to the icon (may be empty)
    std::string tooltip;   // multi-line hover text
    std::string iconName;  // freedesktop icon name
    // Hosts that ignore the text label (KDE, XFCE, ...) get the number drawn into the icon instead.
    bool usePixmap = false;
    std::string pixText;   // e.g. "93" (drawn when usePixmap)
    bool pixCharging = false;
    bool pixLow = false;
    std::vector<MenuDevice> devices;
    bool autostart = false;
};

struct Callbacks {
    std::function<void(const std::string& key)> toggleDevice;
    std::function<void()> refresh;
    std::function<void(bool enable)> setAutostart;
    std::function<void()> quit;
};

bool Init(const Callbacks& cb);  // needs a running session bus; call from the GLib main thread
void Update(const State& s);
void Notify(const std::string& title, const std::string& body);  // desktop notification
void Shutdown();

}  // namespace tray
