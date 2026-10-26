#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Install the packages scripts/package-linux.sh wrote in DIST into clean
# containers, then compile and run a consumer twice: through pkg-config,
# which links the static library, and against libmaelys_datalog_shared.so.
# The consumer reads a SMALL bound, so a LARGE build would fail it.
#
# Usage: tools/check_linux_packages.sh DIST [IMAGE...]
#   IMAGE  debian:*, ubuntu:* (the .deb) or fedora:*, rockylinux:* (the .rpm);
#          default: debian:12 ubuntu:24.04 fedora:42 rockylinux:9
set -euo pipefail

if [ $# -lt 1 ]; then
  echo 'usage: tools/check_linux_packages.sh DIST [IMAGE...]' >&2; exit 2
fi
dist="$(cd "$1" && pwd)"; shift
images=("$@")
[ ${#images[@]} -gt 0 ] || images=(debian:12 ubuntu:24.04 fedora:42 rockylinux:9)
command -v docker >/dev/null || { echo 'error: docker is required' >&2; exit 1; }

version="$(cat "$(dirname "$0")/../VERSION")"
case "$(uname -m)" in
  x86_64|amd64) deb_arch=amd64; rpm_arch=x86_64 ;;
  aarch64|arm64) deb_arch=arm64; rpm_arch=aarch64 ;;
  *) echo "error: unsupported arch $(uname -m)" >&2; exit 1 ;;
esac
deb="libmaelys-datalog-dev_${version}_${deb_arch}.deb"
rpm="maelys-datalog-devel-${version}-1.${rpm_arch}.rpm"

consumer='
set -eu
cat >/tmp/smoke.c <<EOF
#include <maelys/datalog.h>
#include <stddef.h>
int main(void) {
  size_t bound = 0;
  if (maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED,
                               &bound) != MAELYS_DATALOG_STATUS_OK) return 1;
  return bound == 64 ? 0 : 2;
}
EOF
test "$(pkg-config --modversion maelys-datalog)" = "$VERSION"
cc -std=c11 /tmp/smoke.c $(pkg-config --cflags --libs maelys-datalog) -o /tmp/static
/tmp/static
cc -std=c11 /tmp/smoke.c -lmaelys_datalog_shared -o /tmp/shared
/tmp/shared
ldd /tmp/shared | grep -q libmaelys_datalog_shared.so
'

for image in "${images[@]}"; do
  case "$image" in
    debian:*|ubuntu:*)
      file="$deb"
      install='export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends gcc pkg-config "/dist/$FILE" >/dev/null' ;;
    fedora:*|rockylinux:*)
      file="$rpm"
      install='dnf install -y -q gcc pkgconf-pkg-config "/dist/$FILE" >/dev/null' ;;
    *) echo "error: no package format for $image" >&2; exit 1 ;;
  esac
  test -f "$dist/$file" || { echo "error: $dist/$file is missing" >&2; exit 1; }
  docker run --rm --platform "linux/$deb_arch" -v "$dist:/dist:ro" -e FILE="$file" -e VERSION="$version" "$image" \
    sh -c "$install
$consumer"
  echo "ok $image: $file installs; static and shared consumers run"
done
