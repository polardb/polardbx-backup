#####################################
Name:          t-rds-xtrabackup-80
Version:       8.0.1
Release:       %(echo $RELEASE)%{?dist}
Summary:       XtraBackup online backup for MySQL / InnoDB

Group:         Applications/Databases
License:       GPLv2
URL:           http://gitlab.alibaba-inc.com/rds_mysql/rds_xtrabackup_80

BuildRequires: cmake >= 2.8.12, libaio-devel, libgcrypt-devel, ncurses-devel, readline-devel
BuildRequires: zlib-devel, libev-devel, libcurl-devel
BuildRequires: devtoolset-7-gcc
BuildRequires: devtoolset-7-gcc-c++
BuildRequires: devtoolset-7-binutils

%if "%{?dist}" == ".alios7" || "%{?dist}" == ".el7"
BuildRequires: python-sphinx >= 1.0.1, python-docutils >= 0.6
%define os_version 7
%endif
%if "%{?dist}" == ".alios6" || "%{?dist}" == ".el6"
%define os_version 6
%endif

%define use_gcc devtoolset


Requires:      rsync
BuildRoot:     %{_tmppath}/%{name}-%{version}-%{release}-root
Packager:      xiangluo.wb@alibaba-inc.com
Provides:      t-rds-xtrabackup-80 = %{version}
AutoReqProv:   no
Prefix:        /u01/xtrabackup80

%define _mandir /usr/share/man
# do not strip binary files, just compress man doc
%define __os_install_post /usr/lib/rpm/brp-compress

%description
Percona XtraBackup is OpenSource online (non-blockable) backup solution for InnoDB and XtraDB engines

%build
cd $OLDPWD/../
#

if [ "%{use_gcc}" == "system" ]; then
    CC=gcc
    CXX=g++
else
    CC=/opt/rh/devtoolset-7/root/usr/bin/gcc
    CXX=/opt/rh/devtoolset-7/root/usr/bin/g++
fi
CFLAGS="-O3 -g -fexceptions -static-libgcc -fno-omit-frame-pointer -fno-strict-aliasing"
CXXFLAGS="-O3 -g -fexceptions -static-libgcc -fno-omit-frame-pointer -fno-strict-aliasing"

export CC CXX CFLAGS CXXFLAGS

#
cmake -DBUILD_CONFIG=xtrabackup_release -DCMAKE_BUILD_TYPE="RelWithDebInfo" \
      -DCMAKE_INSTALL_PREFIX=%{prefix} -DBUILD_MAN_OS=%{os_version}  \
      -DINSTALL_MANDIR=%{_mandir} -DWITH_BOOST="extra/boost/boost_1_66_0.tar.gz" \
      -DINSTALL_PLUGINDIR="%{prefix}/lib/xtrabackup/plugin" .
#
# make %{?_smp_mflags}
make -j

%install
cd $OLDPWD/../
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
# cp COPYING $RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{prefix}/bin/xtrabackup
%{prefix}/bin/xbstream
%{prefix}/bin/xbcrypt
%{prefix}/bin/xbcloud
%{prefix}/bin/xbcloud_osenv
%{prefix}/lib//xtrabackup/plugin/keyring_file.so
# %{prefix}/bin/mysqlbinlogtailor
# %if "%{os_version}" == "5" || "%{os_version}" == "6"
# %doc COPYING
# %endif
# %if "%{os_version}" == "6" || "%{os_version}" == "7"
# %doc %{_mandir}/man1/*.1.gz
# %endif

%changelog
* Fri Sep 07 2018 Fungo Wang <xiangluo.wb@alibaba-inc.com>
- Packaging for RXB 8.0

* Fri Aug 31 2018 Evgeniy Patlan <evgeniy.patlan@percona.com>
- Packaging for 8.0

* Wed Feb 03 2016 Tomislav Plavcic <tomislav.plavcic@percona.com>
- Packaging updates for version 2.4.0-rc1

* Mon Dec 14 2015 Tomislav Plavcic <tomislav.plavcic@percona.com>
- Update to new release Percona XtraBackup 2.3.3

* Fri Oct 16 2015 Tomislav Plavcic <tomislav.plavcic@percona.com>
- Update to new release Percona XtraBackup 2.3.2
- Renamed the package to percona-xtrabackup since 2.3 became GA

* Fri May 15 2015 Tomislav Plavcic <tomislav.plavcic@percona.com>
- Update to new release Percona XtraBackup 2.3.1beta1

* Thu Oct 30 2014 Tomislav Plavcic <tomislav.plavcic@percona.com>
- Update to new release Percona XtraBackup 2.3.0alpha1

* Wed Sep 29 2014 Tomislav Plavcic <tomislav.plavcic@percona.com>
- Update to new release Percona XtraBackup 2.2.6

* Fri Sep 26 2014 Tomislav Plavcic <tomislav.plavcic@percona.com>
- Update to new release Percona XtraBackup 2.2.5

* Thu Sep 11 2014 Tomislav Plavcic <tomislav.plavcic@percona.com>
- Changed options to build with system zlib

* Mon Jun 10 2014 Tomislav Plavcic <tomislav.plavcic@percona.com>
- renamed package from percona-xtrabackup-22 to percona-xtrabackup

* Wed Mar 26 2014 Alexey Bychko <alexey.bychko@percona.com>
- initial alpha release for 2.2 (2.2.1-alpha1)
