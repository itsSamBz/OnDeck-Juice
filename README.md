# Wireless Battery (Linux)

Battery levels of your wireless mouse, headset, keyboard, and controller — right in your panel/taskbar on Linux.

Shows a StatusNotifierItem (the standard freedesktop tray/indicator protocol) with a text label like `🎧 30%  🖱 93%` and a per-device menu. Works with GNOME (AppIndicator extension), KDE Plasma, Cinnamon, XFCE, Zorin OS, and any desktop that supports the StatusNotifierItem spec.

> **This is the Linux port** of [Wireless Battery for Windows](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor). It shares the vendor driver layer but uses a fully native Linux UI (GLib/GIO, libudev, D-Bus) with no Win32 dependencies.

---

## Features

- **Tiny**: a ~1 MB binary, ~4 MB RAM. Plain C++ with no runtime daemon.
- **No vendor software needed**: talks directly to HID devices. G HUB, Synapse, iCUE and others can stay closed (or keep running — they don't conflict).
- **Many brands**: Logitech, Razer, SteelSeries, Corsair, HyperX, Glorious, PlayStation controllers, and more (see table below).
- **Low battery notification** at 15% via the desktop notification system.
- **Autostart at login** via an XDG autostart `.desktop` file.
- **CLI mode**: `--list` and `--dump` for scripting and device debugging.

---

## Installation

### Option A — AppImage (no build needed)

Download **`WirelessBattery-x86_64.AppImage`** from [Releases](../../releases/latest), make it executable, and run it:

```sh
chmod +x WirelessBattery-x86_64.AppImage
./WirelessBattery-x86_64.AppImage --install-udev   # once, needs your password
./WirelessBattery-x86_64.AppImage
```

The `--install-udev` step installs the udev rule so your user can read HID devices without root. You only need to do it once; unplug and re-plug dongles afterwards if they aren't picked up immediately.

### Option B — Build from source

**Dependencies** (Debian/Ubuntu):
```sh
sudo apt install g++ make pkg-config libglib2.0-dev libudev-dev
```

**Arch Linux:**
```sh
sudo pacman -S gcc make pkgconf glib2 systemd-libs
```

**Build and install:**
```sh
make
./packaging/install.sh          # installs binary + .desktop entry + udev rule
```

Or install to a custom prefix:
```sh
make install PREFIX=~/.local
```

The udev rule (`packaging/70-wireless-battery.rules`) is installed to `/etc/udev/rules.d/` and lets the logged-in user read HID devices without root. Run `./packaging/install.sh --uninstall` to remove everything.

---

## Usage

```
wireless-battery              # run the panel indicator (background)
wireless-battery --list       # print every device and its battery level, then exit
wireless-battery --dump       # print every HID collection (useful for adding a new device)
wireless-battery --quit       # close the running instance
```

The tray menu lets you:
- Hide/show individual devices from the label
- Refresh now
- Enable/disable start at login
- Quit

Levels refresh every 60 seconds, and immediately when a device is plugged in or the system wakes from sleep.

---

## Supported devices

| Vendor | Devices | Protocol |
|---|---|---|
| **Logitech** | Mice and keyboards on Unifying, Bolt, Lightspeed and Nano receivers, over USB or Bluetooth; G headsets (G533, G535, G633/G635/G733/G933/G935, G PRO/PRO X) | HID++ 1.0 / 2.0 |
| **Razer** | Wireless mice, keyboards and dongles; BlackShark V3 / V3 Pro headsets | Razer feature-report protocol |
| **SteelSeries** | Arctis 1/7/7+/9/Pro/Pro Wireless, Arctis Nova 3P/5/7/7X/7P/Pro Wireless, GameBuds; Aerox 3/5/9 Wireless, Prime Wireless, Rival 3 Wireless, Rival 650, Rival Pro | Vendor HID reports |
| **Corsair** | Void / Void Pro / Void Elite / HS70 / HS80 wireless; Virtuoso, Void V2, HS80 MAX, Slipstream mice and keyboards | Vendor HID reports |
| **Cooler Master** | MM712, MM731 (2.4 GHz dongle or USB cable) | Vendor HID status report |
| **HyperX** | Cloud III Wireless, Cloud III S Wireless, Cloud II Wireless, Cloud II Core Wireless, Cloud Alpha Wireless, Cloud Alpha 2 Wireless, Cloud Flight, Cloud Flight S | Vendor HID reports |
| **Glorious** | Model O / O- / D / D- Wireless, Model O 2 / D 2 PRO | Vendor feature report |
| **Hitscan** | Hyperlight X, Hyperlight N | Vendor feature report |
| **Kysona / Pulsar / ATK (VXE) / Hitscan** | Kysona M600 / Mercury, Pulsar and ATK VXE wireless mice (e.g. VXE X3 Pro, ATK F1 Extreme) | Shared vendor protocol |
| **G-Wolves** | Fenrir, Fenrir Pro / Max / Asym, HTX, HTS, HSK, HTR, HT-S2, Lycan, WARG, VUK and 8K dongles | Glorious or Kysona protocol |
| **Keychron** | M4 and other Keychron mice on the 2.4 GHz receiver; 8K mice on the TurboLink 8K receiver | Vendor report |
| **LAMZU** | Maya X, Maya, Maya M, Tachi, Inca, Paro, Thorn, Atlantis Mini / OG Champion, Orcus | Vendor feature report |
| **Redragon** | M916 Pro 1K, M913 Impact Elite, G49 | Kysona protocol |
| **AULA** | F75 / F87 Pro / F99 wireless keyboards on 2.4 GHz receiver | Vendor HID report |
| **ASUS ROG** | Strix Scope II 96, Azoth, Falchion keyboards; Keris, Harpe, Gladius, Chakram, Spatha, Pugio, Strix Impact mice; Omni Receiver | ROG HID battery query |
| **ZOWIE** | EC1/EC2/EC3-CW, DW series (U2-DW, S2-DW …) | Community-documented vendor commands |
| **Rapoo** | Various wireless mice and keyboards | Rapoo protocol |
| **GameSir** | Nova wireless controllers | Vendor HID reports |
| **Sony** | DualShock 4, DualSense (PS4/PS5 controllers) over USB or Bluetooth | Linux `hid-playstation` layout |
| **Sony InZone** | InZone H3 / H7 / H9 wireless headsets | Vendor HID reports |
| **Rongyuan** | Attack Shark and similar receivers | Vendor HID reports |

---

## Building an AppImage

Requires the same build dependencies as above, plus `curl` (to download `appimagetool` if not on `PATH`):

```sh
./packaging/build-appimage.sh
```

Or inside Docker (for maximum portability, targeting older glibc):

```sh
./packaging/build-appimage-docker.sh
```

The AppImage is written to `WirelessBattery-<arch>.AppImage`.

---

## Adding a device

See [CONTRIBUTING.md](CONTRIBUTING.md). The short version:

1. Run `wireless-battery --dump` and find your device's vendor/product IDs and HID collections.
2. Find the battery protocol (HeadsetControl, OpenRazer, Solaar, rivalcfg, Wireshark+USB capture).
3. Create or extend a driver in `src/drivers/<vendor>.cpp`.
4. Declare and call it from `src/devices.cpp`.
5. Add a row to the table above in `README.md`.

---

## License

[GNU General Public License v3.0 or later](LICENSE).
