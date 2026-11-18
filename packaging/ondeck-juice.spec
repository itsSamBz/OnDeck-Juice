Name:           ondeck-juice
Version:        %{?pkg_version}%{!?pkg_version:1.0.0}
Release:        1%{?dist}
Summary:        Battery levels of wireless mice, headsets, keyboards and controllers

License:        GPL-3.0-or-later
URL:            https://github.com/itsSamBz/OnDeck-Juice
Provides:       wireless-battery = %{version}-%{release}

Requires:       glib2
Requires:       systemd-libs

%description
OnDeck-Juice is a lightweight Linux tray monitor that polls wireless peripheral
battery levels via direct HID queries and displays them in your panel as a
StatusNotifierItem.

%prep
# No prep needed when pre-built or building from source tree

%build
# Built via make

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/scalable/apps
mkdir -p %{buildroot}/usr/lib/udev/rules.d

install -m 755 %{_sourcedir}/wireless-battery %{buildroot}%{_bindir}/wireless-battery
ln -sf wireless-battery %{buildroot}%{_bindir}/ondeck-juice

install -m 644 %{_sourcedir}/packaging/wireless-battery.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/wireless-battery.svg
ln -sf wireless-battery.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/ondeck-juice.svg

install -m 644 %{_sourcedir}/packaging/70-wireless-battery.rules %{buildroot}/usr/lib/udev/rules.d/70-wireless-battery.rules

cat > %{buildroot}%{_datadir}/applications/wireless-battery.desktop <<EOF
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

%files
%license LICENSE
%{_bindir}/wireless-battery
%{_bindir}/ondeck-juice
%{_datadir}/applications/wireless-battery.desktop
%{_datadir}/icons/hicolor/scalable/apps/wireless-battery.svg
%{_datadir}/icons/hicolor/scalable/apps/ondeck-juice.svg
/usr/lib/udev/rules.d/70-wireless-battery.rules

%post
udevadm control --reload-rules 2>/dev/null || :
udevadm trigger --subsystem-match=hidraw --subsystem-match=tty --action=change 2>/dev/null || :

%postun
udevadm control --reload-rules 2>/dev/null || :

%changelog
* Wed Nov 18 2026 itsSamBz <https://github.com/itsSamBz/OnDeck-Juice> - 1.0.0-1
- Initial release for Linux
