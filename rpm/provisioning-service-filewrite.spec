Name:       provisioning-service
Summary:    OTA provisioning service
Version:    0.0.1
Release:    1
Group:      Communications/Telephony and IM
License:    GPLv2
# TODO: check the URL
URL:        https://github.com
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(libwbxml2)
Requires:  libxml2
Requires:  libwbxml2
Requires:  dbus
Requires:  ofono

%description
A service for handling over-the-air (OTA) provisioning messages

%prep
%setup -q -n %{name}-%{version}

%build
make %{?_smp_mflags} FILEWRITE=%{_sharedstatedir}/provisioning_service/

%install
rm -rf %{buildroot}
%make_install

mkdir -p %{buildroot}/%{_sharedstatedir}/provisioning_service/

%files
%defattr(-,root,root,-)
%{_libexecdir}/provisioning-service
%config %{_sysconfdir}/dbus-1/system.d/provisioning.conf
%{_datadir}/dbus-1/system-services/org.nemomobile.provisioning.service
%config %{_sysconfdir}/ofono/push_forwarder.d/ofono-provisioning.conf
%defattr(-,root,root,-)
%dir %attr(755,root,root) %{_sharedstatedir}/provisioning_service/
