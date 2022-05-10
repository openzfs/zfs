
%define relabel_files() \
restorecon -R /usr/lib64/security/pam_zfs_key.so; \
restorecon -R /dev/zfs; \
restorecon -R /var/run/pam_zfs_key; \

%define selinux_policyver 34.23-1

Name:           @PACKAGE@
Version:        @VERSION@
Release:        @RELEASE@%{?dist}
Summary:        SELinux policy module for pam_zfs_key

Name:		pam_zfs_key-selinux
Version:	1.0
Release:	1%{?dist}


License:        @ZFS_META_LICENSE@
URL:            https://github.com/openzfs/zfs
Source0:        pam_zfs_key.pp
Source1:        pam_zfs_key.if


Requires: policycoreutils, libselinux-utils
Requires(post): selinux-policy-base >= %{selinux_policyver}, policycoreutils
Requires(postun): policycoreutils
BuildArch: noarch

%description
This package installs and sets up the SELinux policy security module for
pam_zfs_key.

%install
install -d %{buildroot}%{_datadir}/selinux/packages
install -m 644 %{SOURCE0} %{buildroot}%{_datadir}/selinux/packages
install -d %{buildroot}%{_datadir}/selinux/devel/include/contrib
install -m 644 %{SOURCE1} %{buildroot}%{_datadir}/selinux/devel/include/contrib/
install -d %{buildroot}/etc/selinux/targeted/contexts/users/


%post
semodule -n -i %{_datadir}/selinux/packages/pam_zfs_key.pp
if /usr/sbin/selinuxenabled ; then
    /usr/sbin/load_policy
    %relabel_files

fi;
exit 0

%postun
if [ $1 -eq 0 ]; then
    semodule -n -r pam_zfs_key
    if /usr/sbin/selinuxenabled ; then
       /usr/sbin/load_policy
       %relabel_files

    fi;
fi;
exit 0

%files
%attr(0600,root,root) %{_datadir}/selinux/packages/pam_zfs_key.pp
%{_datadir}/selinux/devel/include/contrib/pam_zfs_key.if


%changelog
* Thu Jan 13 2022 YOUR NAME <YOUR@EMAILADDRESS> 1.0-1
- Initial version
