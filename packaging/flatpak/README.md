# Flatpak Packaging for OnDeck-Juice

This directory contains the Flatpak manifest (`io.github.itssambz.OnDeckJuice.yml`) and AppStream metadata for OnDeck-Juice.

## Important Note on HID Permissions & udev

Flatpak runs inside a sandboxed container. While `--device=all` gives the application permission to access `/dev/hidraw*` character devices, the host system must still grant read/write access to your user account.

On Linux, non-root users typically do not have permission to read USB/HID dongles unless a udev rule is installed.

Before or after installing the Flatpak, install the host udev rule:
```bash
sudo install -m 644 packaging/70-wireless-battery.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw --subsystem-match=tty --action=change
```

## Building locally

1. Install `flatpak-builder` and the Freedesktop runtime:
   ```bash
   flatpak install flathub org.freedesktop.Platform//24.08 org.freedesktop.Sdk//24.08
   ```
2. Build and install:
   ```bash
   ./packaging/build-flatpak.sh
   ```
3. Run:
   ```bash
   flatpak run io.github.itssambz.OnDeckJuice
   ```

## Publishing to Flathub

To publish on [Flathub](https://flathub.org):
1. Fork [flathub/flathub](https://github.com/flathub/flathub) on GitHub.
2. Submit a new repository request with `io.github.itssambz.OnDeckJuice.yml` pointing to your tagged release tarball on GitHub.
