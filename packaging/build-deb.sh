#!/usr/bin/env bash
# Builds a Debian package: dist/ondeck-juice_<version>_<arch>.deb
# Needs: dpkg-deb, g++, make, pkg-config, libglib2.0-dev, libudev-dev
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT_DIR="$(pwd)"

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    if git describe --tags --match "v*" >/dev/null 2>&1; then
        VERSION="$(git describe --tags --match "v*" | sed 's/^v//; s/-.*//')"
    else
        VERSION="1.0.0"
    fi
fi

ARCH="$(dpkg --print-architecture 2>/dev/null || uname -m)"
case "$ARCH" in
    x86_64) ARCH="amd64" ;;
    aarch64) ARCH="arm64" ;;
esac

echo "Building OnDeck-Juice (wireless-battery) v$VERSION for $ARCH..."
make -j"$(nproc 2>/dev/null || echo 2)"

STAGE="$ROOT_DIR/build/deb-staging"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN"
mkdir -p "$STAGE/usr/bin"
mkdir -p "$STAGE/usr/share/applications"
mkdir -p "$STAGE/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$STAGE/usr/share/doc/ondeck-juice"
mkdir -p "$STAGE/lib/udev/rules.d"

# Binaries & symlink
install -m 755 "$ROOT_DIR/wireless-battery" "$STAGE/usr/bin/wireless-battery"
ln -sf wireless-battery "$STAGE/usr/bin/ondeck-juice"

# Icons & Desktop file
install -m 644 "$ROOT_DIR/packaging/wireless-battery.svg" "$STAGE/usr/share/icons/hicolor/scalable/apps/wireless-battery.svg"
ln -sf wireless-battery.svg "$STAGE/usr/share/icons/hicolor/scalable/apps/ondeck-juice.svg"

cat > "$STAGE/usr/share/applications/wireless-battery.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=OnDeck Juice
GenericName=Wireless Battery Monitor
Comment=Battery levels of wireless mice, headsets, keyboards and controllers
Exec=wireless-battery
Icon=wireless-battery
Terminal=false
Categories=Utility;HardwareSettings;
Keywords=battery;wireless;headset;mouse;keyboard;gamepad;
StartupNotify=false
EOF

# Udev rule
install -m 644 "$ROOT_DIR/packaging/70-wireless-battery.rules" "$STAGE/lib/udev/rules.d/70-wireless-battery.rules"

# Docs & license
install -m 644 "$ROOT_DIR/LICENSE" "$STAGE/usr/share/doc/ondeck-juice/copyright"

# Debian control file
cat > "$STAGE/DEBIAN/control" <<EOF
Package: ondeck-juice
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: itsSamBz <https://github.com/itsSamBz/OnDeck-Juice>
Homepage: https://github.com/itsSamBz/OnDeck-Juice
Depends: libc6 (>= 2.34), libgcc-s1, libglib2.0-0 (>= 2.56.0), libstdc++6, libudev1
Recommends: gnome-shell-extension-appindicator
Provides: wireless-battery
Conflicts: wireless-battery
Replaces: wireless-battery
Description: Battery levels of wireless mice, headsets, keyboards and controllers
 OnDeck-Juice is a lightweight Linux tray monitor that polls wireless peripheral
 battery levels via direct HID queries and displays them in your panel as a
 StatusNotifierItem. Includes udev rules to allow reading HID devices without root.
EOF

# Maintainer scripts for udev reload
cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules || true
        udevadm trigger --subsystem-match=hidraw --subsystem-match=tty --action=change || true
    fi
fi
EOF
chmod 755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules || true
    fi
fi
EOF
chmod 755 "$STAGE/DEBIAN/postrm"

mkdir -p "$ROOT_DIR/dist"
OUTPUT="$ROOT_DIR/dist/ondeck-juice_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$OUTPUT"

echo "Successfully built: $OUTPUT"
