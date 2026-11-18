#!/bin/sh
# Builds WirelessBattery-<arch>.AppImage. Needs the normal build dependencies (see README) and
# appimagetool (downloaded to build/ if it is not on PATH).
set -e
cd "$(dirname "$0")/.."
ARCH="$(uname -m)"
make
rm -rf build/AppDir
mkdir -p build/AppDir/usr/bin build/AppDir/usr/share/wireless-battery build/AppDir/usr/share/applications
install -m 755 wireless-battery build/AppDir/usr/bin/
install -m 755 packaging/AppRun build/AppDir/AppRun
install -m 644 packaging/70-wireless-battery.rules build/AppDir/usr/share/wireless-battery/
install -m 644 packaging/wireless-battery.svg build/AppDir/wireless-battery.svg
cat > build/AppDir/wireless-battery.desktop <<EOF
[Desktop Entry]
Type=Application
Name=Wireless Battery
Comment=Battery levels of wireless mice, headsets, keyboards and controllers
Exec=AppRun
Icon=wireless-battery
Terminal=false
Categories=Utility;
EOF
cp build/AppDir/wireless-battery.desktop build/AppDir/usr/share/applications/

TOOL="$(command -v appimagetool || true)"
if [ -z "$TOOL" ]; then
    TOOL=build/appimagetool
    if [ ! -x "$TOOL" ]; then
        echo "Downloading appimagetool..."
        curl -fL -o "$TOOL" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage"
        chmod +x "$TOOL"
    fi
fi
rm -f "WirelessBattery-$ARCH.AppImage"  # unlinking works even while the old one is running
# --appimage-extract-and-run: works without FUSE
ARCH="$ARCH" "$TOOL" --appimage-extract-and-run build/AppDir "WirelessBattery-$ARCH.AppImage"
mkdir -p dist
cp -f "WirelessBattery-$ARCH.AppImage" "dist/WirelessBattery-$ARCH.AppImage"
cp -f "WirelessBattery-$ARCH.AppImage" "dist/OnDeck-Juice-$ARCH.AppImage"
echo "Built WirelessBattery-$ARCH.AppImage and dist/OnDeck-Juice-$ARCH.AppImage"
