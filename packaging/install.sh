#!/bin/sh
# Installs Wireless Battery for the current user (binary, launcher entry) and, with sudo, the udev rule.
#   ./packaging/install.sh [--prefix DIR]   install binary + menu entry (default ~/.local), then the udev rule
#   ./packaging/install.sh --udev-only      only install the udev rule (needs root)
#   ./packaging/install.sh --uninstall
# Safe to run as "sudo ./packaging/install.sh": the user files still go to the invoking user's home.
set -e
cd "$(dirname "$0")/.."
SELF="$(pwd)/packaging/install.sh"
PREFIX=""
MODE=all
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift ;;
        --udev-only) MODE=udev ;;
        --user-only) MODE=user ;;
        --uninstall) MODE=uninstall ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

RULE=/etc/udev/rules.d/70-wireless-battery.rules

install_udev() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "Installing the udev rule needs root; running sudo..."
        sudo "$SELF" --udev-only
        return
    fi
    install -m 644 packaging/70-wireless-battery.rules "$RULE"
    udevadm control --reload-rules
    udevadm trigger --subsystem-match=hidraw --subsystem-match=tty --action=change
    echo "udev rule installed. If a dongle is still not readable, unplug and re-plug it."
}

install_user() {
    [ -n "$PREFIX" ] || PREFIX="$HOME/.local"
    [ -x ./wireless-battery ] || make
    mkdir -p "$PREFIX/bin" "$HOME/.local/share/applications"
    install -m 755 wireless-battery "$PREFIX/bin/wireless-battery"
    cat > "$HOME/.local/share/applications/wireless-battery.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Wireless Battery
Comment=Battery levels of wireless mice, headsets, keyboards and controllers
Exec=$PREFIX/bin/wireless-battery
Icon=battery
Terminal=false
Categories=Utility;
EOF
    echo "Installed $PREFIX/bin/wireless-battery"
}

case "$MODE" in
    udev) install_udev ;;
    user) install_user ;;
    uninstall)
        [ -n "$PREFIX" ] || PREFIX="$HOME/.local"
        pkill -x wireless-battery 2>/dev/null || true
        rm -f "$PREFIX/bin/wireless-battery" "$HOME/.local/share/applications/wireless-battery.desktop" \
              "$HOME/.config/autostart/wireless-battery.desktop"
        if [ "$(id -u)" -eq 0 ]; then rm -f "$RULE"; else echo "Remove the udev rule with: sudo rm $RULE"; fi
        ;;
    all)
        if [ "$(id -u)" -eq 0 ] && [ -n "$SUDO_USER" ] && [ "$SUDO_USER" != root ]; then
            install_udev
            # build / install the user part as the invoking user, not root
            if [ -n "$PREFIX" ]; then sudo -u "$SUDO_USER" "$SELF" --user-only --prefix "$PREFIX"
            else sudo -u "$SUDO_USER" "$SELF" --user-only; fi
        else
            install_user
            install_udev
        fi
        echo "Start it from the app menu, or run: wireless-battery &"
        echo "To start at login: tray menu -> 'Start at login'."
        ;;
esac
