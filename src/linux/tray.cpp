// StatusNotifierItem + com.canonical.dbusmenu implemented directly on GDBus, so no AppIndicator
// library is needed. Hosts: gnome-shell-extension-appindicator (Zorin OS / Ubuntu), KDE, ...
//
// The SNI interface deliberately has no Activate method: hosts then open the menu on a left click.
#include "tray.h"

#include <gio/gio.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace tray {
namespace {

const char* kSniXml = R"xml(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="u" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="OverlayIconName" type="s" access="read"/>
    <property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionMovieName" type="s" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <property name="XAyatanaLabel" type="s" access="read"/>
    <property name="XAyatanaLabelGuide" type="s" access="read"/>
    <property name="XAyatanaOrderingIndex" type="u" access="read"/>
    <signal name="NewTitle"/>
    <signal name="NewIcon"/>
    <signal name="NewAttentionIcon"/>
    <signal name="NewOverlayIcon"/>
    <signal name="NewToolTip"/>
    <signal name="NewStatus"><arg type="s" name="status"/></signal>
    <signal name="XAyatanaNewLabel"><arg type="s" name="label"/><arg type="s" name="guide"/></signal>
  </interface>
</node>)xml";

const char* kMenuXml = R"xml(
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg type="i" name="parentId" direction="in"/>
      <arg type="i" name="recursionDepth" direction="in"/>
      <arg type="as" name="propertyNames" direction="in"/>
      <arg type="u" name="revision" direction="out"/>
      <arg type="(ia{sv}av)" name="layout" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg type="ai" name="ids" direction="in"/>
      <arg type="as" name="propertyNames" direction="in"/>
      <arg type="a(ia{sv})" name="properties" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg type="i" name="id" direction="in"/>
      <arg type="s" name="name" direction="in"/>
      <arg type="v" name="value" direction="out"/>
    </method>
    <method name="Event">
      <arg type="i" name="id" direction="in"/>
      <arg type="s" name="eventId" direction="in"/>
      <arg type="v" name="data" direction="in"/>
      <arg type="u" name="timestamp" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg type="a(isvu)" name="events" direction="in"/>
      <arg type="ai" name="idErrors" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg type="i" name="id" direction="in"/>
      <arg type="b" name="needUpdate" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg type="ai" name="ids" direction="in"/>
      <arg type="ai" name="updatesNeeded" direction="out"/>
      <arg type="ai" name="idErrors" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated">
      <arg type="a(ia{sv})" name="updatedProps"/>
      <arg type="a(ias)" name="removedProps"/>
    </signal>
    <signal name="LayoutUpdated">
      <arg type="u" name="revision"/>
      <arg type="i" name="parent"/>
    </signal>
    <signal name="ItemActivationRequested">
      <arg type="i" name="id"/>
      <arg type="u" name="timestamp"/>
    </signal>
  </interface>
</node>)xml";

const char* kItemPath = "/StatusNotifierItem";
const char* kMenuPath = "/MenuBar";

enum MenuId { ID_ROOT = 0, ID_EMPTY = 1, ID_SEP = 2, ID_REFRESH = 3, ID_AUTOSTART = 4, ID_QUIT = 5, ID_DEVICE0 = 100 };

// 5x7 bitmap font: digits, '%', '?', '+' (rows top to bottom, bit 4 = leftmost pixel).
const unsigned char* Glyph57(char c) {
    static const unsigned char font[][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2},
        {31,16,30,1,1,17,14}, {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12},
        {24,25,2,4,8,19,3}, {14,17,1,2,4,0,4}, {0,4,4,31,4,4,0}, {0,0,0,0,0,0,0}};
    if (c >= '0' && c <= '9') return font[c - '0'];
    if (c == '%') return font[10];
    if (c == '?') return font[11];
    if (c == '+') return font[12];
    return font[13];
}

// Square ARGB32 icon (network byte order) with the text in white on a dark outline, readable on any panel.
GVariant* RenderPixmap(const State& s) {
    const int N = 22;
    std::vector<unsigned char> px(N * N * 4, 0);
    auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        if (x < 0 || y < 0 || x >= N || y >= N) return;
        unsigned char* p = &px[(y * N + x) * 4];
        p[0] = a; p[1] = r; p[2] = g; p[3] = b;
    };
    std::string t = s.pixText.substr(0, 3);
    int scale = t.size() <= 2 ? 2 : 1;
    int w = (int)t.size() * 6 * scale - scale, h = 7 * scale;
    int x0 = (N - w) / 2, y0 = (N - h) / 2;
    unsigned char fr = 255, fg = 255, fb = 255;
    if (s.pixLow) { fg = 90; fb = 90; }
    else if (s.pixCharging) { fr = 140; fb = 140; }
    for (int pass = 0; pass < 2; pass++) {  // pass 0: outline, pass 1: fill
        for (size_t i = 0; i < t.size(); i++) {
            const unsigned char* gl = Glyph57(t[i]);
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++) {
                    if (!(gl[row] & (16 >> col))) continue;
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) {
                            int x = x0 + (int)i * 6 * scale + col * scale + dx, y = y0 + row * scale + dy;
                            if (pass == 0) {
                                for (int ox = -1; ox <= 1; ox++)
                                    for (int oy = -1; oy <= 1; oy++) put(x + ox, y + oy, 20, 20, 20, 230);
                            } else {
                                put(x, y, fr, fg, fb, 255);
                            }
                        }
                }
        }
    }
    GBytes* bytes = g_bytes_new(px.data(), px.size());
    GVariant* arr = g_variant_new_from_bytes(G_VARIANT_TYPE("ay"), bytes, TRUE);
    g_bytes_unref(bytes);
    GVariant* one = g_variant_new("(ii@ay)", N, N, arr);
    return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), &one, 1);
}

GDBusConnection* g_bus = nullptr;
guint g_ownId = 0, g_watchId = 0, g_itemReg = 0, g_menuReg = 0;
std::string g_busName;
State g_state;
Callbacks g_cb;
guint g_revision = 1;

struct Item {
    int id;
    std::string label;
    bool enabled = true;
    bool separator = false;
    int toggle = -1;  // -1: plain item, 0/1: checkmark state
};

std::vector<Item> BuildItems() {
    std::vector<Item> items;
    if (g_state.devices.empty()) {
        Item it{ ID_EMPTY, "No devices with battery info found" };
        it.enabled = false;
        items.push_back(it);
    }
    for (size_t i = 0; i < g_state.devices.size(); i++) {
        Item it{ ID_DEVICE0 + (int)i, g_state.devices[i].text };
        it.toggle = g_state.devices[i].shown ? 1 : 0;
        items.push_back(it);
    }
    Item sep{ ID_SEP, "" };
    sep.separator = true;
    items.push_back(sep);
    items.push_back({ ID_REFRESH, "Refresh now" });
    Item as{ ID_AUTOSTART, "Start at login" };
    as.toggle = g_state.autostart ? 1 : 0;
    items.push_back(as);
    items.push_back({ ID_QUIT, "Quit" });
    return items;
}

GVariant* PropsVariant(const Item& it) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    if (it.separator) {
        g_variant_builder_add(&b, "{sv}", "type", g_variant_new_string("separator"));
    } else {
        g_variant_builder_add(&b, "{sv}", "label", g_variant_new_string(it.label.c_str()));
        g_variant_builder_add(&b, "{sv}", "enabled", g_variant_new_boolean(it.enabled));
        g_variant_builder_add(&b, "{sv}", "visible", g_variant_new_boolean(TRUE));
        if (it.toggle >= 0) {
            g_variant_builder_add(&b, "{sv}", "toggle-type", g_variant_new_string("checkmark"));
            g_variant_builder_add(&b, "{sv}", "toggle-state", g_variant_new_int32(it.toggle));
        }
    }
    return g_variant_builder_end(&b);
}

GVariant* EmptyArray(const char* type) { return g_variant_new_array(G_VARIANT_TYPE(type), nullptr, 0); }

GVariant* ItemLayout(const Item& it) {
    return g_variant_new("(i@a{sv}@av)", it.id, PropsVariant(it), EmptyArray("v"));
}

GVariant* Layout(int parent) {
    std::vector<Item> items = BuildItems();
    if (parent != ID_ROOT) {
        for (const Item& it : items)
            if (it.id == parent) return ItemLayout(it);
        return g_variant_new("(i@a{sv}@av)", parent, EmptyArray("{sv}"), EmptyArray("v"));
    }
    GVariantBuilder kids;
    g_variant_builder_init(&kids, G_VARIANT_TYPE("av"));
    for (const Item& it : items) g_variant_builder_add(&kids, "v", ItemLayout(it));
    GVariantBuilder rootProps;
    g_variant_builder_init(&rootProps, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&rootProps, "{sv}", "children-display", g_variant_new_string("submenu"));
    return g_variant_new("(i@a{sv}@av)", ID_ROOT, g_variant_builder_end(&rootProps), g_variant_builder_end(&kids));
}

void Dispatch(int id) {
    switch (id) {
        case ID_REFRESH: if (g_cb.refresh) g_cb.refresh(); break;
        case ID_AUTOSTART: if (g_cb.setAutostart) g_cb.setAutostart(!g_state.autostart); break;
        case ID_QUIT: if (g_cb.quit) g_cb.quit(); break;
        default:
            if (id >= ID_DEVICE0 && (size_t)(id - ID_DEVICE0) < g_state.devices.size() && g_cb.toggleDevice)
                g_cb.toggleDevice(g_state.devices[id - ID_DEVICE0].key);
    }
}

void MenuMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method, GVariant* params,
                GDBusMethodInvocation* inv, gpointer) {
    std::string m = method;
    if (m == "GetLayout") {
        gint32 parent = 0;
        g_variant_get_child(params, 0, "i", &parent);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u@(ia{sv}av))", g_revision, Layout(parent)));
    } else if (m == "GetGroupProperties") {
        GVariant* idsV = g_variant_get_child_value(params, 0);
        std::vector<Item> items = BuildItems();
        GVariantBuilder out;
        g_variant_builder_init(&out, G_VARIANT_TYPE("a(ia{sv})"));
        gsize n = g_variant_n_children(idsV);
        for (const Item& it : items) {
            bool wanted = n == 0;
            for (gsize i = 0; i < n && !wanted; i++) {
                gint32 id;
                g_variant_get_child(idsV, i, "i", &id);
                wanted = id == it.id;
            }
            if (wanted) g_variant_builder_add(&out, "(i@a{sv})", it.id, PropsVariant(it));
        }
        g_variant_unref(idsV);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(a(ia{sv}))", &out));
    } else if (m == "GetProperty") {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(v)", g_variant_new_string("")));
    } else if (m == "Event") {
        gint32 id = 0;
        const gchar* ev = nullptr;
        g_variant_get(params, "(i&svu)", &id, &ev, nullptr, nullptr);
        if (ev && std::string(ev) == "clicked") Dispatch(id);
        g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (m == "EventGroup") {
        GVariant* evs = g_variant_get_child_value(params, 0);
        for (gsize i = 0; i < g_variant_n_children(evs); i++) {
            gint32 id = 0;
            const gchar* ev = nullptr;
            GVariant* data = nullptr;
            guint32 ts = 0;
            GVariant* e = g_variant_get_child_value(evs, i);
            g_variant_get(e, "(i&svu)", &id, &ev, &data, &ts);
            if (ev && std::string(ev) == "clicked") Dispatch(id);
            if (data) g_variant_unref(data);
            g_variant_unref(e);
        }
        g_variant_unref(evs);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ai)", EmptyArray("i")));
    } else if (m == "AboutToShow") {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));
    } else if (m == "AboutToShowGroup") {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ai@ai)", EmptyArray("i"), EmptyArray("i")));
    } else {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method);
    }
}

GVariant* MenuGet(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* prop, GError**, gpointer) {
    std::string p = prop;
    if (p == "Version") return g_variant_new_uint32(3);
    if (p == "TextDirection") return g_variant_new_string("ltr");
    if (p == "Status") return g_variant_new_string("normal");
    if (p == "IconThemePath") return EmptyArray("s");
    return nullptr;
}

GVariant* SniGet(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* prop, GError**, gpointer) {
    std::string p = prop;
    if (p == "Category") return g_variant_new_string("Hardware");
    if (p == "Id") return g_variant_new_string("wireless-battery");
    if (p == "Title") return g_variant_new_string("Wireless Battery");
    if (p == "Status") return g_variant_new_string("Active");
    if (p == "WindowId") return g_variant_new_uint32(0);
    if (p == "IconName") return g_variant_new_string(g_state.usePixmap ? "" : g_state.iconName.c_str());
    if (p == "IconPixmap") return g_state.usePixmap ? RenderPixmap(g_state) : EmptyArray("(iiay)");
    if (p == "OverlayIconPixmap" || p == "AttentionIconPixmap") return EmptyArray("(iiay)");
    if (p == "OverlayIconName" || p == "AttentionIconName" || p == "AttentionMovieName") return g_variant_new_string("");
    if (p == "ToolTip")
        return g_variant_new("(s@a(iiay)ss)", "", EmptyArray("(iiay)"), "Wireless Battery", g_state.tooltip.c_str());
    if (p == "ItemIsMenu") return g_variant_new_boolean(TRUE);
    if (p == "Menu") return g_variant_new_object_path(kMenuPath);
    if (p == "XAyatanaLabel") return g_variant_new_string(g_state.label.c_str());
    if (p == "XAyatanaLabelGuide") return g_variant_new_string("");
    if (p == "XAyatanaOrderingIndex") return g_variant_new_uint32(0);
    return nullptr;
}

void RegisterWithWatcher() {
    g_dbus_connection_call(g_bus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                           "RegisterStatusNotifierItem", g_variant_new("(s)", g_busName.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1,
                           nullptr, nullptr, nullptr);
}

void OnWatcherAppeared(GDBusConnection*, const gchar*, const gchar*, gpointer) { RegisterWithWatcher(); }

// Start looking for a host only once our bus name is ours, so the host can query us right away.
void OnNameAcquired(GDBusConnection*, const gchar*, gpointer) {
    if (g_watchId) return;
    g_watchId = g_bus_watch_name_on_connection(g_bus, "org.kde.StatusNotifierWatcher", G_BUS_NAME_WATCHER_FLAGS_NONE, OnWatcherAppeared,
                                               nullptr, nullptr, nullptr);
}

void Emit(const char* path, const char* iface, const char* signal, GVariant* params = nullptr) {
    g_dbus_connection_emit_signal(g_bus, nullptr, path, iface, signal, params, nullptr);
}

}  // namespace

bool Init(const Callbacks& cb) {
    g_cb = cb;
    GError* err = nullptr;
    g_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (!g_bus) {
        g_printerr("wireless-battery: no D-Bus session bus: %s\n", err ? err->message : "?");
        g_clear_error(&err);
        return false;
    }
    GDBusNodeInfo* sni = g_dbus_node_info_new_for_xml(kSniXml, nullptr);
    GDBusNodeInfo* menu = g_dbus_node_info_new_for_xml(kMenuXml, nullptr);
    GDBusInterfaceVTable sniVt{};
    sniVt.get_property = SniGet;
    GDBusInterfaceVTable menuVt{};
    menuVt.method_call = MenuMethod;
    menuVt.get_property = MenuGet;
    g_itemReg = g_dbus_connection_register_object(g_bus, kItemPath, sni->interfaces[0], &sniVt, nullptr, nullptr, &err);
    if (g_itemReg) g_menuReg = g_dbus_connection_register_object(g_bus, kMenuPath, menu->interfaces[0], &menuVt, nullptr, nullptr, &err);
    g_dbus_node_info_unref(sni);
    g_dbus_node_info_unref(menu);
    if (!g_itemReg || !g_menuReg) {
        g_printerr("wireless-battery: could not export tray object: %s\n", err ? err->message : "?");
        g_clear_error(&err);
        return false;
    }
    g_busName = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
    g_ownId = g_bus_own_name_on_connection(g_bus, g_busName.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE, OnNameAcquired, nullptr, nullptr, nullptr);
    return true;
}

void Update(const State& s) {
    if (!g_bus) return;
    bool iconChanged = s.iconName != g_state.iconName || s.usePixmap != g_state.usePixmap || s.pixText != g_state.pixText ||
                       s.pixCharging != g_state.pixCharging || s.pixLow != g_state.pixLow;
    bool labelChanged = s.label != g_state.label;
    bool tipChanged = s.tooltip != g_state.tooltip;
    g_state = s;
    g_revision++;
    if (iconChanged) Emit(kItemPath, "org.kde.StatusNotifierItem", "NewIcon");
    if (labelChanged) Emit(kItemPath, "org.kde.StatusNotifierItem", "XAyatanaNewLabel", g_variant_new("(ss)", s.label.c_str(), ""));
    if (tipChanged) Emit(kItemPath, "org.kde.StatusNotifierItem", "NewToolTip");
    Emit(kMenuPath, "com.canonical.dbusmenu", "LayoutUpdated", g_variant_new("(ui)", g_revision, 0));
}

void Notify(const std::string& title, const std::string& body) {
    if (!g_bus) return;
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    g_dbus_connection_call(g_bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
                           g_variant_new("(susss@as@a{sv}i)", "Wireless Battery", 0u, "battery-caution-symbolic", title.c_str(), body.c_str(),
                                         EmptyArray("s"), g_variant_builder_end(&hints), 8000),
                           nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
}

void Shutdown() {
    if (!g_bus) return;
    if (g_watchId) g_bus_unwatch_name(g_watchId);
    if (g_ownId) g_bus_unown_name(g_ownId);
    if (g_itemReg) g_dbus_connection_unregister_object(g_bus, g_itemReg);
    if (g_menuReg) g_dbus_connection_unregister_object(g_bus, g_menuReg);
    g_dbus_connection_flush_sync(g_bus, nullptr, nullptr);
    g_watchId = g_ownId = g_itemReg = g_menuReg = 0;
}

}  // namespace tray
