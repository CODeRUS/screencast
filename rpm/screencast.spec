%define theme sailfish-default

%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}

Name:       screencast
Summary:    Sailfish screen cast
Version:    0.3.2
Release:    3
Group:      System/GUI/Other
License:    GPLv2
URL:        https://github.com/coderus/screencast
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Sensors)
BuildRequires:  qt5-qtplatformsupport-devel
BuildRequires:  qt5-qtwayland-wayland_egl-devel
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  pkgconfig(systemd)
BuildRequires:  sailfish-svg2png
BuildRequires:  systemd
%systemd_requires

%description
Lipstick screencast client

%prep
%setup -q -n %{name}-%{version}

%build
%qtc_qmake5 \
  "PROJECT_PACKAGE_VERSION=%{version}" \
  SPEC_UNITDIR=%{_userunitdir}
%qtc_make %{_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

%pre
if [ "$1" = "2" ]; then
systemctl-user stop screencast.socket ||:
systemctl-user stop screencast.service ||:
fi

%post
systemctl-user daemon-reload ||:
systemctl-user enable screencast.socket ||:
systemctl-user restart screencast.socket ||:

%preun
if [ "$1" = "0" ]; then
systemctl-user stop screencast.socket ||:
systemctl-user stop screencast.service ||:
fi

%files
%defattr(-,root,root,-)
%attr(2755, root, privileged) %{_sbindir}/screencast

%{_datadir}/themes/%{theme}/silica

%{_userunitdir}/screencast.service
%{_userunitdir}/screencast.socket

%{_datadir}/jolla-settings/entries/screencast.json
%{_datadir}/jolla-settings/pages/screencast/mainpage.qml

%{_datadir}/translations
