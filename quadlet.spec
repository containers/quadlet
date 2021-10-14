Name:           quadlet
Version:        0.2.1
Release:        1%{?dist}
Summary:        Systemd container integration tool

License:        GPLv2+
URL:            https://github.com/containers/quadlet
Source0:        %{url}/archive/%{version}/%{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  gcc
BuildRequires:  pkgconfig(glib-2.0) >= 2.44.0
BuildRequires:  pkgconfig(gobject-2.0)

Requires(pre):  /usr/sbin/useradd
Requires:       podman
Requires:       crun

%description
Quadlet is an opinionated tool for easily running podman system containers under systemd in an optimal way.

%prep
%autosetup

%build
%meson
%meson_build

%check
%meson_test

%install
%meson_install

%pre
# We create a quadlet user so that we can get subuids and subgids allocated.
# It really is a system user, but Unfortunately useradd  doesn't create subuids
# for system users, so we manually make it system-like and start at a higher
# min uid to avoid conflicts with common uid nrs around 1000
getent passwd quadlet >/dev/null || \
    useradd -M -U -K SUB_UID_COUNT=65536 -K UID_MIN=50000 \
    -s /sbin/nologin -d /nonexisting \
    -c "User for quadlet" quadlet
exit 0

%files
%license COPYING
%doc README.md
%doc docs/Fileformat.md
%doc docs/ContainerSetup.md
%{_libexecdir}/quadlet-generator
%_prefix/lib/systemd/system-generators/quadlet-system-generator
%_prefix/lib/systemd/user-generators/quadlet-user-generator

%changelog
* Mon Sep 27 2021 Alexander Larsson <alexl@redhat.com> - 0.1.0-1
- Initial version
