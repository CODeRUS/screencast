%define theme sailfish-default

%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}

Name:       screencast
Summary:    Sailfish screen cast
Version:    0.3.0
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
BuildRequires:  systemd
%systemd_requires

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
%systemd_user_post screencast.socket
%systemd_user_post screencast.service

%preun
%systemd_user_preun screencast.socket
%systemd_user_preun screencast.service

%postun
%systemd_user_postun screencast.socket
%systemd_user_postun screencast.service

%files
%defattr(-,root,root,-)
%attr(2755, root, privileged) %{_sbindir}/screencast

%{_datadir}/themes/%{theme}/meegotouch/z1.0/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z1.25/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z1.5/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z1.5-large/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z1.75/icons/*.png
%{_datadir}/themes/%{theme}/meegotouch/z2.0/icons/*.png

%{_libdir}/systemd/user/screencast.service
%{_libdir}/systemd/user/screencast.socket

%{_datadir}/jolla-settings/entries/screencast.json
%{_datadir}/jolla-settings/pages/screencast/mainpage.qml
