Name:       provisioning-service
Summary:    OTA provisioning service
Version:    0.1.7
Release:    1
License:    GPLv2
URL:        https://github.com/sailfishos/provisioning-service
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libwbxml2) >= 0.11.6
BuildRequires:  pkgconfig(libgofono) >= 2.0.5
BuildRequires:  pkgconfig(libglibutil)
BuildRequires:  pkgconfig(systemd)
Requires:  libgofono >= 2.0.5
Requires:  ofono
Requires:  sailfish-setup

%description
A service for handling over-the-air (OTA) provisioning messages

%prep
%setup -q -n %{name}-%{version}

%build
make generate
make %{?_smp_mflags} KEEP_SYMBOLS=1 release

%check
make -C test test

%install
%make_install

%files
%defattr(-,root,root,-)
%license COPYING
%{_libexecdir}/provisioning-service
%{_unitdir}/*.service
%{_sysconfdir}/dbus-1/system.d/provisioning.conf
%{_datadir}/dbus-1/system-services/org.nemomobile.provisioning.service
%{_sysconfdir}/ofono/push_forwarder.d/ofono-provisioning.conf
