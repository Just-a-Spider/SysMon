Name:           sysmon-server
Version:        0.1.0
Release:        1%{?dist}
Summary:        3DS Hardware Companion Server
License:        MIT
BuildRequires:  gtk3-devel, libxdo-devel

%description
Background service and macro executor for the SysMon 3DS application.

%build
# Pre-compiled outside of rpmbuild

%install
install -D -m 0755 %{_topdir}/../target/release/sysmon-server %{buildroot}/%{_bindir}/sysmon-server
install -D -m 0644 %{_topdir}/../packaging/sysmon-server.desktop %{buildroot}/%{_datadir}/applications/sysmon-server.desktop

%files
%{_bindir}/sysmon-server
%{_datadir}/applications/sysmon-server.desktop
