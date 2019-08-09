%define theme sailfish-default

%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}

Name:       screencast
Summary:    Sailfish screen cast
Version:    0.2.5
Release:    1
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

%description
Lipstick screencast client

%prep
%setup -q -n %{name}-%{version}

%build
%qtc_qmake5 \
  "PROJECT_PACKAGE_VERSION=%{version}"
%qtc_make %{_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

%pre
if [ "$1" = "2" ]; then
systemctl stop screencast.socket ||:
systemctl stop screencast.service ||:
fi

%post
if [ "$1" = "1" ]; then
systemctl daemon-reload ||:
systemctl enable screencast.socket ||:
systemctl restart screencast.socket ||:
fi

%preun
if [ "$1" = "0" ]; then
systemctl stop screencast.socket ||:
systemctl stop screencast.service ||:
fi

%postun
if [ "$1" = "1" ]; then
systemctl restart screencast.socket ||:
fi

%files
%defattr(-,root,root,-)
%attr(755, root, root) %{_sbindir}/screencast

%{_datadir}/themes/%{theme}/meegotouch/z1.0/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z1.25/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z1.5/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z1.5-large/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z1.75/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z2.0/icons/*.png

/lib/systemd/system/screencast.service
/lib/systemd/system/screencast.socket

%{_datadir}/jolla-settings/entries/screencast.json
%{_datadir}/jolla-settings/pages/screencast/mainpage.qml
