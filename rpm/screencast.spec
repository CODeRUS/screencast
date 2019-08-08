%define theme sailfish-default

%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}

Name:       screencast
Summary:    Sailfish screen cast
Version:    0.2.0
Release:    1
Group:      System/GUI/Other
License:    GPLv2
URL:        https://github.com/coderus/screencast
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
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

%post
systemctl stop screencast.socket
systemctl stop screencast.service
systemctl daemon-reload
systemctl enable screencast.socket
systemctl start screencast.socket

%preun
systemctl stop screencast.socket
systemctl stop screencast.service

%postun
systemctl stop screencast.socket
systemctl stop screencast.service


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
