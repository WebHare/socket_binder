Summary: Socket binder
Group: Applications/WebHare
Name: socket_binder
Version: 1.0.1
Release: 1
License: MIT
Source: socket_binder.tar.gz
Url: https://github.com/WebHare/socket_binder
Vendor: B-Lex IT B.V.
BuildRoot: /tmp/%{name}-buildroot
Prereq: /usr/sbin/groupadd

%description
The socket binder allows processes (such as WebHare) to access all
privileged ports (TCP/IP ports numbered below 1024) without requiring the
process itself to have root privileges or special capabilities

%prep
%setup -n socket_binder

%build
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
PREFIX=${RPM_BUILD_ROOT}/usr make

%install
PREFIX=${RPM_BUILD_ROOT}/usr make install
mkdir -p $RPM_BUILD_ROOT/etc/init.d
cp socket_binderctl $RPM_BUILD_ROOT/etc/init.d/socket_binder

%files
%defattr(755,root,root)
/etc/init.d/socket_binder
%attr(4500,root,root) /usr/sbin/socket_binder

%pre
/usr/sbin/groupadd sockbind 2>/dev/null || true

%preun
if [ $1 = 0 ]; then
  /etc/init.d/socket_binder stop
  /sbin/chkconfig --del socket_binder
fi

%post
/sbin/chkconfig --add socket_binder
/etc/init.d/socket_binder start
