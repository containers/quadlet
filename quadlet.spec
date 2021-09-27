Name:           quadlet
Version:        0.1.0
Release:        1%{?dist}
Summary:        Systemd container integration tool

License:        GPLv2+
URL:            https://github.com/containers/quadlet
Source0:        %{url}/archive/%{version}/%{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  gcc
BuildRequires:  pkgconfig(glib-2.0) >= 2.44.0
BuildRequires:  pkgconfig(gobject-2.0)

Requires:       podman

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

%files
%license COPYING
%doc README.md
%doc docs/Fileformat.md
%_prefix/lib/systemd/system-generators/quadlet-generator

%changelog
* Mon Sep 27 2021 Alexander Larsson <alexl@redhat.com> - 0.1.0-1
- Initial version
