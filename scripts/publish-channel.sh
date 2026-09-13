#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Publish one channel of a released tag (docs/conventions.md, "Publishing to a
# registry"). The socle's channel job checks out the tag, downloads the
# release's own assets into dist/ and runs this script with NODE_AUTH_TOKEN
# and packages: write; nothing is rebuilt, the channel ships the bytes the
# release shipped.
#
# Idempotent by contract: a replayed tag runs the channel again on a version
# the registry already holds, and this script must then exit 0 without
# republishing. It records what it did in $CHANNEL_RECORD when the socle
# provides one; those fields join the channel-npm.json marker the socle
# attaches to the release — the observation of a publication, never an
# intention (D3).
#
# `maelys-release rehearse . --channel npm --tag vX.Y.Z` sets CHANNEL_DRY_RUN=1,
# which channel.yml never sets: the script then takes its real publishing
# path — assembly, spec resolution, registry handshake — under
# `npm publish --dry-run`, on a version the registry may already hold. The
# idempotent early exit alone never reaches that path; v0.3.0 died there, on
# a tarball path npm read as a GitHub shorthand, with a rehearsal green.
#
# Usage: scripts/publish-channel.sh TAG CHANNEL
set -eu
cd "$(dirname "$0")/.."
tag="${1:?usage: scripts/publish-channel.sh TAG CHANNEL}"
channel="${2:?usage: scripts/publish-channel.sh TAG CHANNEL}"
version="${tag#v}"
[ "$version" = "$(cat VERSION)" ] \
  || { echo "error: tag $tag does not name VERSION $(cat VERSION)" >&2; exit 1; }
case "$channel" in
  npm) ;;
  *) echo "error: unknown channel: $channel (this product publishes: npm)" >&2; exit 1 ;;
esac

package="@maelys-dev/datalog-wasm"
registry="https://npm.pkg.github.com"
# The dist-tag follows the series (D4/D5): next while 0.x, latest from 1.0.0.
case "$version" in 0.*) dist_tag=next ;; *) dist_tag=latest ;; esac

dry_run=0
[ "${CHANNEL_DRY_RUN:-0}" = 1 ] && dry_run=1

record() {
  [ -n "${CHANNEL_RECORD:-}" ] || return 0
  jq -n --arg registry "$registry" --arg package "$package" --arg version "$version" \
        --arg dist_tag "$dist_tag" --argjson already "$1" \
        '{registry:$registry, package:$package, version:$version, dist_tag:$dist_tag, already_published:$already}' \
    > "$CHANNEL_RECORD"
}

# A private npmrc for this run, never the operator's: the token comes from the
# socle's job and must not outlive it.
[ -n "${NODE_AUTH_TOKEN:-}" ] \
  || { echo "error: NODE_AUTH_TOKEN is required to reach $registry" >&2; exit 1; }
npmrc="$(mktemp)"
trap 'rm -f "$npmrc"' EXIT
printf '@maelys-dev:registry=%s\n//npm.pkg.github.com/:_authToken=%s\n' \
  "$registry" "$NODE_AUTH_TOKEN" > "$npmrc"
export NPM_CONFIG_USERCONFIG="$npmrc"

if [ "$dry_run" = 0 ] && npm view "$package@$version" version >/dev/null 2>&1; then
  echo "$package@$version is already on $registry; nothing to publish (replayed tag)"
  record true
  exit 0
fi

bash scripts/build-npm-package.sh dist/ >/dev/null
# npm reads a bare "dir/name.tgz" as the GitHub shorthand "owner/repo" and
# tries to clone it: v0.3.0's channel job died on "git ls-remote
# ssh://git@github.com/dist/maelys-dev-datalog-wasm-0.3.0.tgz.git". The path
# must start with "./" (or "/") to be taken as a file.
tgz="$(find ./dist -maxdepth 1 -name 'maelys-dev-datalog-wasm-*.tgz' | head -1)"
[ -n "$tgz" ] || { echo "error: no package tarball assembled in dist/" >&2; exit 1; }
case "$tgz" in ./*|/*) ;; *) tgz="./$tgz" ;; esac
if [ "$dry_run" = 1 ]; then
  # npm's dry run still resolves the spec, assembles, authenticates and reads
  # the packument — everything that failed on v0.3.0 — and then refuses a
  # version the registry already holds, exactly as the real publication
  # would. That refusal, on a version npm confirms is held, is the dry run
  # succeeding: the rehearsal runs against a released tag by construction.
  echo "dry run: publishing $package@$version from $tgz to $registry (dist-tag: $dist_tag) with --dry-run"
  if ! npm publish "$tgz" --tag "$dist_tag" --dry-run; then
    npm view "$package@$version" version >/dev/null 2>&1 \
      || { echo "error: the dry run failed on a version $registry does not hold" >&2; exit 1; }
    echo "dry run: $registry already holds $package@$version, as it should for a released tag"
    record true
    exit 0
  fi
else
  echo "publishing $package@$version from $tgz to $registry (dist-tag: $dist_tag)"
  npm publish "$tgz" --tag "$dist_tag"
fi
record false
