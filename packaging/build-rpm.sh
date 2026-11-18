#!/usr/bin/env bash
# Builds an RPM package: dist/ondeck-juice-<version>-1.<arch>.rpm
# Needs: rpmbuild (install with 'sudo apt install rpm' on Ubuntu/Debian, or 'dnf install rpm-build' on Fedora)
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT_DIR="$(pwd)"

if ! command -v rpmbuild >/dev/null 2>&1; then
    echo "Error: rpmbuild is not installed." >&2
    echo "On Debian/Ubuntu: sudo apt install rpm" >&2
    echo "On Fedora/RHEL:   sudo dnf install rpm-build" >&2
    exit 1
fi

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    if git describe --tags --match "v*" >/dev/null 2>&1; then
        VERSION="$(git describe --tags --match "v*" | sed 's/^v//; s/-.*//')"
    else
        VERSION="1.0.0"
    fi
fi

echo "Building OnDeck-Juice (wireless-battery) RPM v$VERSION..."
make -j"$(nproc 2>/dev/null || echo 2)"

RPM_TOP="$ROOT_DIR/build/rpmbuild"
rm -rf "$RPM_TOP"
mkdir -p "$RPM_TOP"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

# Copy files needed by the spec file into SOURCES
cp "$ROOT_DIR/wireless-battery" "$RPM_TOP/SOURCES/"
cp "$ROOT_DIR/LICENSE" "$RPM_TOP/BUILD/"
mkdir -p "$RPM_TOP/SOURCES/packaging"
cp -r "$ROOT_DIR/packaging"/* "$RPM_TOP/SOURCES/packaging/"

rpmbuild -bb \
    --define "_topdir $RPM_TOP" \
    --define "pkg_version $VERSION" \
    "$ROOT_DIR/packaging/ondeck-juice.spec"

mkdir -p "$ROOT_DIR/dist"
find "$RPM_TOP/RPMS" -type f -name "*.rpm" -exec cp {} "$ROOT_DIR/dist/" \;

echo "Successfully built RPM packages:"
ls -lh "$ROOT_DIR/dist/"*.rpm
