#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Build the Debian and RPM packages of one Linux target into DIST:
#   libmaelys-datalog-dev_<v>_<arch>.deb
#   maelys-datalog-devel-<v>-1.<arch>.rpm
#
# Each holds what the Homebrew formula installs: public headers, the static
# library pkg-config links (-lmaelys_datalog), libmaelys_datalog_shared.so,
# maelys-datalog.pc, the conformance header and the extension templates.
# Consumers link the engine statically, so there is no separate runtime
# package. The SMALL profile is built in Release, like the formula, through
# the same CMake install rules. Each format gets its own build because
# pkg-config records the library directory: /usr/lib/<triplet> for Debian,
# /usr/lib64 for RPM.
#
# Usage: scripts/package-linux.sh TARGET DIST   (TARGET: linux-x86_64, linux-arm64)
set -euo pipefail

if [ $# -ne 2 ]; then
  echo 'usage: scripts/package-linux.sh TARGET DIST' >&2; exit 2
fi
target=$1
case "$target" in
  linux-x86_64) deb_arch=amd64; rpm_arch=x86_64; triplet=x86_64-linux-gnu ;;
  linux-arm64)  deb_arch=arm64; rpm_arch=aarch64; triplet=aarch64-linux-gnu ;;
  *) echo "error: no Linux packages for $target" >&2; exit 1 ;;
esac
case "$(uname -s):$(uname -m)" in
  Linux:x86_64) host=linux-x86_64 ;;
  Linux:aarch64|Linux:arm64) host=linux-arm64 ;;
  *) host=other ;;
esac
[ "$host" = "$target" ] || { echo "error: $target packages are built on $target, not $host" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo 'error: dpkg-deb is required' >&2; exit 1; }
command -v rpmbuild >/dev/null || { echo 'error: rpmbuild is required' >&2; exit 1; }

root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$2"
dist="$(cd "$2" && pwd)"
version="$(cat "$root/VERSION")"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct)}"
work="$(mktemp -d "${TMPDIR:-/tmp}/maelys-linux-packages.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}

# Listings are read whole before grep: grep -q closing a pipe early would
# fail the producer under pipefail.
has() { grep -qxF "$2" <<<"$1" || { echo "error: $3 lacks $2" >&2; exit 1; }; }

# stage NAME LIBDIR: configure, build and install SMALL Release under $work/NAME.
stage() {
  local build="$work/build-$1"
  cmake -S "$root" -B "$build" -DBUILD_TESTING=OFF \
    -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_BUILD_TYPE=Release \
    -DMAELYS_DATALOG_REPRODUCIBLE_PATHS=ON -DMAELYS_DATALOG_PROFILE_LARGE=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR="$2" >/dev/null
  cmake --build "$build" --target maelys_datalog maelys_datalog_shared --parallel 4 >/dev/null
  for component in sdk sdk-static sdk-shared; do
    DESTDIR="$work/$1" cmake --install "$build" --component "$component" >/dev/null
  done
}

# ---------------------------------------------------------------------------
# Debian
# ---------------------------------------------------------------------------
stage deb "lib/$triplet"
deb_lib="usr/lib/$triplet"
shared="$work/deb/$deb_lib/libmaelys_datalog_shared.so"
test -f "$shared" || { echo "error: no $deb_lib/libmaelys_datalog_shared.so installed" >&2; exit 1; }
# The highest GLIBC_x.y symbol version the shared library needs.
glibc="$(objdump -T "$shared" | sed -n 's/.*GLIBC_\([0-9][0-9.]*\).*/\1/p' | sort -t. -k1,1n -k2,2n -u | tail -n1)"
test -n "$glibc"

deb="$work/deb"
mkdir -p "$deb/usr/share/doc/libmaelys-datalog-dev" "$deb/DEBIAN"
install -m 0644 "$root/LICENSE" "$deb/usr/share/doc/libmaelys-datalog-dev/copyright"
cat >"$deb/DEBIAN/control" <<EOF
Package: libmaelys-datalog-dev
Version: ${version}
Section: libdevel
Priority: optional
Architecture: ${deb_arch}
Maintainer: Maelys Developers <noreply@maelys.dev>
Depends: libc6 (>= ${glibc}), libc6-dev
Homepage: https://datalog.maelys.dev
Description: C SDK of the Maelys Datalog engine (SMALL profile)
 Public headers, static and shared libraries, pkg-config metadata,
 conformance header and extension templates of the bounded Maelys Datalog
 policy engine, built with the SMALL size profile. pkg-config links the
 static library.
EOF
find "$deb" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
deb_file="libmaelys-datalog-dev_${version}_${deb_arch}.deb"
dpkg-deb --root-owner-group -Zxz --build "$deb" "$dist/$deb_file" >/dev/null
( cd "$dist" && sha256 "$deb_file" >"$deb_file.sha256" )
deb_list="$(dpkg-deb --contents "$dist/$deb_file" | awk '{print $NF}')"
for path in "./$deb_lib/libmaelys_datalog.a" "./$deb_lib/libmaelys_datalog_shared.so" \
    "./$deb_lib/pkgconfig/maelys-datalog.pc" ./usr/include/maelys/datalog.h; do
  has "$deb_list" "$path" "$deb_file"
done
echo "packaged $deb_file (libc6 >= $glibc)"

# ---------------------------------------------------------------------------
# RPM
# ---------------------------------------------------------------------------
stage rpm lib64
rpm_top="$work/rpmbuild"
mkdir -p "$rpm_top"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
cat >"$rpm_top/SPECS/maelys-datalog.spec" <<EOF
Name: maelys-datalog-devel
Version: ${version}
Release: 1
Summary: C SDK of the Maelys Datalog engine (SMALL profile)
License: MPL-2.0
URL: https://datalog.maelys.dev
BuildArch: ${rpm_arch}

%description
Public headers, static and shared libraries, pkg-config metadata,
conformance header and extension templates of the bounded Maelys Datalog
policy engine, built with the SMALL size profile. pkg-config links the
static library.

%prep
%build
%install
mkdir -p %{buildroot}
cp -a ${work}/rpm/. %{buildroot}/
install -D -m 0644 ${root}/LICENSE %{buildroot}/usr/share/licenses/maelys-datalog-devel/LICENSE

%files
/usr/include/maelys/
/usr/lib64/libmaelys_datalog.a
/usr/lib64/libmaelys_datalog_shared.so
/usr/lib64/pkgconfig/maelys-datalog.pc
/usr/share/maelys-datalog/
%license /usr/share/licenses/maelys-datalog-devel/LICENSE
EOF
rpmbuild --define "_topdir $rpm_top" --define "_buildhost maelys-datalog" \
  --define "use_source_date_epoch_as_buildtime 1" --define "build_mtime_policy clamp_to_source_date_epoch" \
  --define "_build_id_links none" --define "debug_package %{nil}" --define "__strip /bin/true" \
  --quiet -bb "$rpm_top/SPECS/maelys-datalog.spec" >/dev/null
rpm_file="maelys-datalog-devel-${version}-1.${rpm_arch}.rpm"
cp "$rpm_top/RPMS/$rpm_arch/$rpm_file" "$dist/$rpm_file"
( cd "$dist" && sha256 "$rpm_file" >"$rpm_file.sha256" )
rpm_list="$(rpm -qlp "$dist/$rpm_file")"
for path in /usr/lib64/libmaelys_datalog.a /usr/lib64/libmaelys_datalog_shared.so \
    /usr/lib64/pkgconfig/maelys-datalog.pc /usr/include/maelys/datalog.h; do
  has "$rpm_list" "$path" "$rpm_file"
done
echo "packaged $rpm_file"
