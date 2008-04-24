# spl
%define name    spl 
%define version 0.2.1

Summary: Solaris Porting Layer
Name: %{name}
Version: %{version}
Release: 1
Copyright: GPL
Group: Utilities/System
BuildRoot: /tmp/%{name}-%{version}
Source: %{name}-%{version}.tar.gz

%description
Abstration layer to provide Solaris style primatives in the linux kernel.

%prep
%setup -q
./configure

%build
rm -rf $RPM_BUILD_ROOT
make

%install
make install "DESTDIR=$RPM_BUILD_ROOT"

%files
