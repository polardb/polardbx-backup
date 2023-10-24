#####################################
Name:          t-polardb-x-xtrabackup
Version:       8.0.32
Release:       %(echo $RELEASE)%{?dist}
Summary:       XtraBackup online backup for Polardb-X Engine

Group:         Applications/Databases
License:       GPLv2
URL:           http://gitlab.alibaba-inc.com/GalaxyEngine/percona-xtrabackup

BuildRequires: cmake >= 2.8.12, libaio-devel, libgcrypt-devel, ncurses-devel, readline-devel
BuildRequires: zlib-devel, libev-devel, libcurl-devel
BuildRequires: devtoolset-7-gcc
BuildRequires: devtoolset-7-gcc-c++
BuildRequires: devtoolset-7-binutils
BuildRequires: bison

%if "%{?dist}" == ".alios7" || "%{?dist}" == ".el7"
%define os_version 7
%endif

Requires:      rsync
BuildRoot:     %{_tmppath}/%{name}-%{version}-%{release}-root
Packager:      jianhua.sjh@alibaba-inc.com
Provides:      t-polardb-x-xtrabackup = %{version}
AutoReqProv:   no
Prefix:        /u01/polardb_x_xtrabackup

%define _mandir /usr/share/man
# do not strip binary files, just compress man doc
%define __os_install_post /usr/lib/rpm/brp-compress

%description
Percona XtraBackup is OpenSource online (non-blockable) backup solution for InnoDB and XtraDB engines

%build
cd $OLDPWD/../

CC=/opt/rh/devtoolset-7/root/usr/bin/gcc
CXX=/opt/rh/devtoolset-7/root/usr/bin/g++
CFLAGS="-O3 -g -fexceptions -static-libgcc -fno-omit-frame-pointer -fno-strict-aliasing"
CXXFLAGS="-O3 -g -fexceptions -static-libgcc -fno-omit-frame-pointer -fno-strict-aliasing"
export CC CXX CFLAGS CXXFLAGS

cat extra/boost/boost_1_77_0.tar.bz2.*  > extra/boost/boost_1_77_0.tar.bz2

cmake -DBUILD_CONFIG=xtrabackup_release -DCMAKE_BUILD_TYPE="RelWithDebInfo" \
      -DCMAKE_INSTALL_PREFIX=%{prefix} -DBUILD_MAN_OS=%{os_version}  \
      -DINSTALL_MANDIR=%{_mandir} -DWITH_BOOST="extra/boost/boost_1_77_0.tar.gz" \
      -DINSTALL_PLUGINDIR="lib/plugin" \
      -DFORCE_INSOURCE_BUILD=1 .

make %{?_smp_mflags}

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
%{prefix}/lib/plugin/keyring_file.so
%{prefix}/lib/plugin/keyring_rds.so
%{prefix}/bin/mysqlbinlogtailor

%changelog


