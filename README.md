# OnDeck-Juice (Wireless Battery for Linux)

Battery levels of your wireless mouse, headset, keyboard, and controller — right in your panel/taskbar on Linux.

Shows a StatusNotifierItem (the standard freedesktop tray/indicator protocol) with a text label like `🎧 30%  🖱 93%` and a per-device menu. Works with GNOME (AppIndicator extension), KDE Plasma, Cinnamon, XFCE, Zorin OS, and any desktop environment supporting the StatusNotifierItem spec.

> **This is the dedicated Linux port** of [Wireless Battery](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor). It shares the device driver layer but uses a fully native Linux UI (GLib/GIO, libudev, D-Bus) with zero Win32 dependencies.

---

## Features

- **Lightweight**: native C++ binary (~1 MB), minimal memory usage (~4 MB RAM), no background runtime daemon.
- **Direct hardware queries**: talks directly to HID devices. Vendor software (G HUB, Synapse, iCUE) is not needed (and does not conflict if running).
- **Extensive hardware support**: Logitech, Razer, SteelSeries, Corsair, HyperX, Glorious, PlayStation controllers, and many more (see [supported devices table](#supported-devices)).
- **Desktop notifications**: alerts at 15% low battery via the desktop notification daemon.
- **Autostart support**: toggleable directly from the tray context menu.
- **CLI utility**: `--list` and `--dump` for terminal scripts and device inspection.

---

## Installation & Release Formats

Pre-built packages for all major distributions are available on the **[Releases](../../releases/latest)** page:

### 1. AppImage (Universal)
Works on any modern Linux distribution without installation:
```bash
chmod +x WirelessBattery-x86_64.AppImage
./WirelessBattery-x86_64.AppImage --install-udev   # One-time setup for device permissions
./WirelessBattery-x86_64.AppImage
```

### 2. Debian / Ubuntu (`.deb`)
Download `ondeck-juice_<version>_amd64.deb` from Releases:
```bash
sudo dpkg -i ondeck-juice_*_amd64.deb
# or
sudo apt install ./ondeck-juice_*_amd64.deb
```
> *Note: The `.deb` package installs the udev rule automatically into `/lib/udev/rules.d/` and triggers permission reloading.*

### 3. Fedora / RHEL / openSUSE (`.rpm`)
Download `ondeck-juice-<version>-1.x86_64.rpm` from Releases:
```bash
sudo dnf install ./ondeck-juice-*.rpm
# or on openSUSE:
sudo zypper install ./ondeck-juice-*.rpm
```

### 4. Arch Linux / AUR
The `PKGBUILD` is located in [`packaging/aur/`](packaging/aur/):
```bash
# Using an AUR helper (once published):
yay -S ondeck-juice

# Or manually from this repository:
cd packaging/aur
makepkg -si
```

### 5. Flatpak
A Flatpak manifest is provided in [`packaging/flatpak/`](packaging/flatpak/):
```bash
./packaging/build-flatpak.sh
flatpak run io.github.itssambz.OnDeckJuice
```
> *Note: Because Flatpak runs in a sandbox, make sure the host udev rule is installed (`sudo make install` or copying `packaging/70-wireless-battery.rules` to `/etc/udev/rules.d/`).*

### 6. Build from Source
**Build dependencies:**
- **Ubuntu/Debian:** `sudo apt install g++ make pkg-config libglib2.0-dev libudev-dev`
- **Fedora:** `sudo dnf install gcc-c++ make pkgconf-pkg-config glib2-devel systemd-devel`
- **Arch Linux:** `sudo pacman -S gcc make pkgconf glib2 systemd-libs`

```bash
make
./packaging/install.sh          # installs binary + .desktop entry + udev rule
```

Uninstall anytime:
```bash
./packaging/install.sh --uninstall
```

---

## Usage

```bash
wireless-battery              # Run the panel tray indicator (alias: ondeck-juice)
wireless-battery --list       # Print all connected devices and their battery level, then exit
wireless-battery --dump       # Print all HID collections (useful for device reverse-engineering)
wireless-battery --quit       # Terminate the running instance
```

The tray menu allows you to:
- Toggle device visibility in the panel text label
- Force an instant battery refresh
- Toggle start at login
- Gracefully quit

Battery levels refresh every 60 seconds automatically, and immediately whenever a device is plugged in or the system wakes from suspend.

---

## Supported Devices

| Vendor | Devices | Protocol |
|---|---|---|
| **Logitech** | Mice and keyboards on Unifying, Bolt, Lightspeed, and Nano receivers, over USB or Bluetooth; G headsets (G533, G535, G633/G635/G733/G933/G935, G PRO/PRO X) | HID++ 1.0 / 2.0 |
| **Razer** | Wireless mice, keyboards, and dongles; BlackShark V3 / V3 Pro headsets | Razer feature-report protocol |
| **SteelSeries** | Arctis 1/7/7+/9/Pro/Pro Wireless, Arctis Nova 3P/5/7/7X/7P/Pro Wireless, GameBuds; Aerox 3/5/9 Wireless, Prime Wireless, Rival 3 Wireless, Rival 650, Rival Pro | Vendor HID reports |
| **Corsair** | Void / Void Pro / Void Elite / HS70 / HS80 wireless; Virtuoso, Void V2, HS80 MAX, Slipstream mice and keyboards | Vendor HID reports |
| **Cooler Master** | MM712, MM731 (2.4 GHz dongle or USB cable) | Vendor HID status report |
| **HyperX** | Cloud III Wireless, Cloud III S Wireless, Cloud II Wireless, Cloud II Core Wireless, Cloud Alpha Wireless, Cloud Alpha 2 Wireless, Cloud Flight, Cloud Flight S | Vendor HID reports |
| **Glorious** | Model O / O- / D / D- Wireless, Model O 2 / D 2 PRO | Vendor feature report |
| **Hitscan** | Hyperlight X, Hyperlight N | Vendor feature report |
| **Kysona / Pulsar / ATK (VXE) / Hitscan** | Kysona M600 / Mercury, Pulsar, and ATK VXE wireless mice (e.g. VXE X3 Pro, ATK F1 Extreme) | Shared vendor protocol |
| **G-Wolves** | Fenrir, Fenrir Pro / Max / Asym, HTX, HTS, HSK, HTR, HT-S2, Lycan, WARG, VUK, and 8K dongles | Glorious or Kysona protocol |
| **Keychron** | M4 and other Keychron mice on 2.4 GHz receiver; 8K mice on TurboLink 8K receiver | Vendor report |
| **LAMZU** | Maya X, Maya, Maya M, Tachi, Inca, Paro, Thorn, Atlantis Mini / OG Champion, Orcus | Vendor feature report |
| **Redragon** | M916 Pro 1K, M913 Impact Elite, G49 | Kysona protocol |
| **AULA** | F75 / F87 Pro / F99 wireless keyboards on 2.4 GHz receiver | Vendor HID report |
| **ASUS ROG** | Strix Scope II 96, Azoth, Falchion keyboards; Keris, Harpe, Gladius, Chakram, Spatha, Pugio, Strix Impact mice; Omni Receiver | ROG HID battery query |
| **ZOWIE** | EC1/EC2/EC3-CW, DW series (U2-DW, S2-DW …) | Community-documented vendor commands |
| **Rapoo** | Wireless mice and keyboards | Rapoo protocol |
| **GameSir** | Nova wireless controllers | Vendor HID reports |
| **Sony** | DualShock 4, DualSense (PS4/PS5 controllers) over USB or Bluetooth | Linux `hid-playstation` layout |
| **Sony InZone** | InZone H3 / H7 / H9 wireless headsets | Vendor HID reports |
| **Rongyuan** | Attack Shark and similar receivers | Vendor HID reports |

---

## Contributing & Adding Devices

See [CONTRIBUTING.md](CONTRIBUTING.md) for full instructions:
1. Run `wireless-battery --dump` to discover the vendor/product IDs and HID usages.
2. Identify the battery protocol.
3. Add a driver in `src/drivers/<vendor>.cpp`.
4. Register the driver in `src/devices.cpp`.
5. Update `README.md`.

---

## License

[GNU General Public License v3.0 or later](LICENSE).
