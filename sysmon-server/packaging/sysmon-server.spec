Name:           sysmon-server
Version:        0.3.3
Release:        1%{?dist}
Summary:        SysMon Server for Nintendo 3DS Companion
License:        MIT
BuildRequires:  gtk3-devel, libxdo-devel

%description
Background telemetry server, gamepad driver, and macro executor for the SysMon 3DS application.

%build
# Pre-compiled outside of rpmbuild

%install
install -D -m 0755 %{_topdir}/../target/release/sysmon-server %{buildroot}/%{_bindir}/sysmon-server
install -D -m 0644 %{_topdir}/../packaging/sysmon-server.desktop %{buildroot}/%{_datadir}/applications/sysmon-server.desktop
install -D -m 0644 %{_topdir}/../packaging/99-sysmon-uinput.rules %{buildroot}/%{_prefix}/lib/udev/rules.d/99-sysmon-uinput.rules
install -D -m 0644 %{_topdir}/../../sysmon-3ds/icon.png %{buildroot}/%{_datadir}/icons/hicolor/512x512/apps/sysmon-server.png

%files
%{_bindir}/sysmon-server
%{_datadir}/applications/sysmon-server.desktop
%{_prefix}/lib/udev/rules.d/99-sysmon-uinput.rules
%{_datadir}/icons/hicolor/512x512/apps/sysmon-server.png
