#!/bin/sh
# Builds the AppImage inside an old distro (Ubuntu 20.04, glibc 2.31) so it runs on nearly every
# current distro. Needs a running Docker daemon.
set -e
cd "$(dirname "$0")/.."
IMAGE="${IMAGE:-ubuntu:20.04}"
docker run --rm -v "$(pwd)":/src -w /src -e DEBIAN_FRONTEND=noninteractive -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$IMAGE" sh -c '
    apt-get update -qq &&
    apt-get install -y -qq g++ make pkg-config libglib2.0-dev libudev-dev curl file squashfs-tools ca-certificates >/dev/null &&
    rm -rf build wireless-battery &&
    ./packaging/build-appimage.sh &&
    chown -R "$HOST_UID:$HOST_GID" build wireless-battery WirelessBattery-*.AppImage'
