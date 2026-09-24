#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Validate the real tarball, not a second hand-built staging fixture.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo 'usage: tools/check_sdk_archive.sh CMAKE_BUILD [ARCHIVE]' >&2; exit 2
fi
build="$(cd "$1" && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/maelys-archive-check.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT
if [[ $# == 2 ]]; then
  archive="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
else
  archive="$scratch/sdk.tar.gz"
  bash "$root/scripts/package-native-sdk.sh" "$build" "$archive"
fi
# Install the same selected components independently; full CMake installations
# are exercised by check_module_sdk.sh in the same CI legs.
for component in sdk sdk-static; do
  cmake --install "$build" --prefix "$scratch/install" --component "$component"
done
mkdir "$scratch/extracted"
tar -xzf "$archive" -C "$scratch/extracted"
prefix="$scratch/extracted"
# BSD tar can absorb AppleDouble metadata on extraction. Inspect raw members
# too: no file or duplicate entry may disappear from the extracted inventory.
normalize_members() {
  sed -e 's@^\./@@' -e 's@/$@@' -e '/^\.$/d' -e '/^$/d' | LC_ALL=C sort
}
check_raw_members() {
  python3 - "$1" <<'PY' | normalize_members > "$scratch/members"
import sys
import tarfile

# Unlike BSD tar's listing, the format reader retains AppleDouble members.
with tarfile.open(sys.argv[1], "r:gz") as archive:
    for member in archive:
        print(member.name)
PY
  (cd "$prefix" && find . -print) | normalize_members > "$scratch/extracted-members"
  diff -u "$scratch/members" "$scratch/extracted-members"
}
check_raw_members "$archive"
diff -r "$scratch/install/include" "$prefix/include"
diff -r "$scratch/install/share" "$prefix/share"
diff -r "$scratch/install/lib" "$prefix/lib"
if [[ "$(uname -s)" == Darwin ]]; then
  # Comparing two installs alone can pass accidentally within the same second.
  # The first ar member is the symbol index: its decimal timestamp must be zero.
  python3 - "$prefix/lib/libmaelys_datalog.a" <<'PY'
import sys

with open(sys.argv[1], "rb") as archive:
    header = archive.read(68)  # ar magic (8) + first member header (60)
assert header[:8] == b"!<arch>\n", "invalid static archive"
assert int(header[24:36].strip()) == 0, "non-deterministic ranlib index timestamp"
PY
fi
cmp "$root/LICENSE" "$prefix/LICENSE"
cmp "$root/CHANGELOG.md" "$prefix/CHANGELOG.md"
cmp "$root/vendor/yyjson/LICENSE" "$prefix/licenses/yyjson/LICENSE"
# Reject extra top-level payload as well as accidentally shipped shared libraries.
actual="$(cd "$prefix" && find . -type f | LC_ALL=C sort)"
expected="$(
  cd "$scratch/install"
  { find . -type f; printf '%s\n' './LICENSE' './CHANGELOG.md' './licenses/yyjson/LICENSE'; } | LC_ALL=C sort
)"
[[ "$actual" == "$expected" ]] || { echo 'FAIL: unexpected archive payload' >&2; exit 1; }
bash "$root/tools/check_module_sdk.sh" --prefix "$prefix" --static-only

# A public runtime accessor must agree with the selected CMake profile; a LARGE
# test must not accidentally validate a second SMALL library.
case "$(sed -n 's/^MAELYS_DATALOG_PROFILE_LARGE:BOOL=//p' "$build/CMakeCache.txt")" in
  ON) limit=256 ;;
  OFF) limit=64 ;;
  *) echo 'FAIL: unknown CMake memory profile' >&2; exit 1 ;;
esac
cp "$root/tests/fixtures/sdk_profile.c" "$scratch/"
(
  cd "$scratch"
  unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$prefix/include" \
    -DEXPECT_FACTS_PER_PRED="$limit" sdk_profile.c \
    "$prefix/lib/libmaelys_datalog.a" -o profile
  ./profile
)

# Mutation checks: fail early on the exact missing-header defect and on a
# private-header leak, even if that private header would itself fail to compile.
mv "$prefix/include/maelys/datalog_details.h" "$scratch/details.h"
if bash "$root/tools/check_module_sdk.sh" --prefix "$prefix" --static-only > "$scratch/missing.log" 2>&1; then
  echo 'FAIL: missing details header was accepted' >&2; exit 1
fi
grep -q datalog_details.h "$scratch/missing.log"
mv "$scratch/details.h" "$prefix/include/maelys/datalog_details.h"
cp "$root/include/maelys_datalog.h" "$prefix/include/"
if bash "$root/tools/check_module_sdk.sh" --prefix "$prefix" --static-only > "$scratch/private.log" 2>&1; then
  echo 'FAIL: private aggregation header was accepted' >&2; exit 1
fi
grep -q maelys_datalog.h "$scratch/private.log"
rm "$prefix/include/maelys_datalog.h"
# Portable raw-inventory mutation: a duplicate of an identical member leaves the
# extracted tree unchanged, but must still be rejected by the format-level guard.
python3 - "$archive" "$scratch/duplicate.tar.gz" <<'PY'
import sys
import tarfile

with tarfile.open(sys.argv[1], "r:gz") as source, tarfile.open(sys.argv[2], "w:gz") as target:
    duplicate = None
    for member in source:
        target.addfile(member, source.extractfile(member) if member.isfile() else None)
        if member.name.endswith("/datalog_details.h"):
            duplicate = member
    assert duplicate is not None
    target.addfile(duplicate, source.extractfile(duplicate))
PY
if check_raw_members "$scratch/duplicate.tar.gz" > "$scratch/duplicate.log" 2>&1; then
  echo 'FAIL: duplicate raw archive member was accepted' >&2; exit 1
fi
grep -q datalog_details.h "$scratch/duplicate.log"
echo "SDK archive: install parity, external consumers, profile $limit, missing/private/duplicate member mutations PASS"
