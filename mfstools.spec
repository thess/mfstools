# .SPEC-file to package RPMs for Fedora and CentOS

%define project_base_url https://github.com/thess
%define completions_dir %(pkgconf  --silence-errors --variable=completionsdir bash-completion)

# build like this:
# spectool -g -R SPECS/mfstools.spec
# rpmbuild -ba SPECS/mfstools.spec

Summary: Utilities for working with TiVO DVR storage
Name: mfstools
%global latest_tag %(git ls-remote -q --tags --refs %{project_base_url}/%{name}.git | awk -F/ '$0 !~ /rc/ {ver=$NF; gsub("v", "", ver)} END{print ver}')
Version: %(echo %latest_tag | tr '-' '.')
Release: 1
License: GPLv3
URL: %{project_base_url}/%{name}
Source: %{project_base_url}/%{name}/archive/refs/tags/v%{latest_tag}.zip
BuildRequires: automake

%description
Utilities for working with TiVO DVR storage

%prep
%autosetup -n %{name}-%{latest_tag}
autoreconf -iv

%build
%configure
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix}

%files
%{!?_licensedir:%global license %%doc}
%{_bindir}/mfsadd
%{_bindir}/mls
%{_bindir}/supersize
%{_bindir}/mfsd
%{_bindir}/backup
%{_bindir}/restore
%{_bindir}/mfscopy
%{_bindir}/mfsinfo
%{_bindir}/mfsck
%{_bindir}/mfstool
%{_bindir}/mfsaddfix
%{_bindir}/8TBprep
%{_bindir}/bootsectorfix
%{_bindir}/apmfix

