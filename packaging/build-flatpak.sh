#!/usr/bin/env bash
# Builds and installs OnDeck-Juice using flatpak-builder
# Needs: flatpak, flatpak-builder
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT_DIR="$(pwd)"

if ! command -v flatpak-builder >/dev/null 2>&1; then
    echo "Error: flatpak-builder is not installed." >&2
    echo "Install it with:" >&2
    echo "  Ubuntu/Debian: sudo apt install flatpak-builder" >&2
    echo "  Fedora:        sudo dnf install flatpak-builder" >&2
    echo "  Arch Linux:    sudo pacman -S flatpak-builder" >&2
    exit 1
fi

echo "Building Flatpak package..."
flatpak-builder --force-clean \
    --user --install \
    "$ROOT_DIR/build/flatpak-build" \
    "$ROOT_DIR/packaging/flatpak/io.github.itssambz.OnDeckJuice.yml"

echo "Flatpak built and installed locally!"
echo "Run with: flatpak run io.github.itssambz.OnDeckJuice"
