#!/usr/bin/env bash
#
# Assemble (et éventuellement publie) le paquet npm @maelys-dev/datalog-wasm
# à partir des tarballs WASM DÉJÀ CONSTRUITS présents dans dist/.
#
# Registre : GitHub Packages (npm.pkg.github.com), fixé dans publishConfig.
# Le scope @maelys-dev est imposé par ce registre — il doit être celui du
# propriétaire du dépôt. Pas de `--provenance` : c'est propre à npmjs.com ;
# ici la provenance est portée par les attestations des tarballs.
#
# Ce script ne compile RIEN : il extrait des octets attestés, écrit un
# package.json, et laisse `npm pack` produire le tarball. C'est la limite
# admise pour le job `publish` du workflow release (voir
# docs/release-engineering.md, D5 et invariant 5) : manipulation d'octets
# déjà construits, jamais d'exécution de code candidat.
#
# Usage:
#   scripts/build-npm-package.sh dist/            # assemble + npm pack (local)
#   scripts/build-npm-package.sh dist/ --publish  # assemble + npm publish
#                                                 #   vers GitHub Packages
#                                                 #   dist-tag: next si prérelease,
#                                                 #   latest sinon
#
# La version vient du reçu (dist/release-receipt.json) si présent, sinon du
# fichier VERSION — jamais d'un argument, pour qu'on ne puisse pas publier
# une version qui ne correspond pas aux artefacts.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

dist="${1:-dist}"
publish=0
[ "${2:-}" = "--publish" ] && publish=1

[ -d "$dist" ] || { echo "erreur: répertoire d'artefacts introuvable: $dist" >&2; exit 1; }
dist="$(cd "$dist" && pwd)"   # chemin absolu : accepte dist/ relatif ou absolu

# ── Version : reçu d'abord, VERSION sinon ────────────────────────────────────
if [ -f "$dist/release-receipt.json" ]; then
  version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
    "$dist/release-receipt.json")"
else
  version="$(cat VERSION)"
  echo "note: pas de release-receipt.json dans $dist — version lue depuis VERSION ($version)" >&2
fi

small_tar="$dist/maelys-datalog-${version}-wasm-small.tar.gz"
large_tar="$dist/maelys-datalog-${version}-wasm-large.tar.gz"
for t in "$small_tar" "$large_tar"; do
  [ -f "$t" ] || { echo "erreur: tarball WASM manquant: $t" >&2; exit 1; }
done

# ── Assemblage dans un staging jetable ───────────────────────────────────────
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
pkg="$stage/package"
mkdir -p "$pkg/small" "$pkg/large"

tar -xzf "$small_tar" -C "$pkg/small"
tar -xzf "$large_tar" -C "$pkg/large"

# Le wrapper est identique dans les deux profils : le remonter à la racine.
if [ -f "$pkg/small/maelys_playground.js" ]; then
  mv "$pkg/small/maelys_playground.js" "$pkg/maelys_playground.js"
  rm -f "$pkg/large/maelys_playground.js"
fi
if [ -f "$pkg/small/maelys_playground.d.ts" ]; then
  mv "$pkg/small/maelys_playground.d.ts" "$pkg/maelys_playground.d.ts"
  rm -f "$pkg/large/maelys_playground.d.ts"
fi

cp LICENSE "$pkg/LICENSE"
mkdir -p "$pkg/licenses/yyjson"
cp vendor/yyjson/LICENSE "$pkg/licenses/yyjson/"

cat > "$pkg/README.md" <<EOF
# @maelys-dev/datalog-wasm

Maelys Datalog ${version} — the bounded, stratified-negation Datalog engine,
compiled to WebAssembly. Two build profiles are shipped:

- \`small/\` — default bounds (EDB 1024)
- \`large/\` — extended bounds (\`-DMAELYS_DATALOG_PROFILE_LARGE\`)

Each profile contains the Emscripten module (\`maelys_datalog_dynamic.js\`)
and its \`.wasm\` binary. \`maelys_playground.js\` is the ergonomic wrapper
used by the interactive playground at the project site.

These artifacts are built and attested by the tagged release workflow of
https://github.com/maelys-dev/maelys-datalog — see the release receipt
attached to the corresponding GitHub Release.

This package is published to GitHub Packages, not to npmjs.com. Consumers
point the scope at that registry and authenticate with a GitHub token that
has \`read:packages\` — GitHub Packages requires authentication even for
public packages:

\`\`\`
@maelys-dev:registry=https://npm.pkg.github.com
//npm.pkg.github.com/:_authToken=\${GITHUB_TOKEN}
\`\`\`

License: MPL-2.0. The vendored yyjson parser keeps its own MIT license, in
\`licenses/yyjson/LICENSE\`.
EOF

cat > "$pkg/package.json" <<EOF
{
  "name": "@maelys-dev/datalog-wasm",
  "version": "${version}",
  "description": "Maelys Datalog engine (bounded, stratified-negation Datalog in pure C) compiled to WebAssembly — small and large build profiles plus the playground wrapper.",
  "license": "MPL-2.0",
  "repository": {
    "type": "git",
    "url": "git+https://github.com/maelys-dev/maelys-datalog.git"
  },
  "homepage": "https://github.com/maelys-dev/maelys-datalog#readme",
  "publishConfig": { "registry": "https://npm.pkg.github.com" },
  "keywords": ["datalog", "policy", "authorization", "wasm", "webassembly", "embedded"],
  "main": "small/maelys_datalog_dynamic.js",
  "exports": {
    ".": "./small/maelys_datalog_dynamic.js",
    "./small": "./small/maelys_datalog_dynamic.js",
    "./large": "./large/maelys_datalog_dynamic.js",
    "./playground": "./maelys_playground.js",
    "./small/maelys_datalog_dynamic.wasm": "./small/maelys_datalog_dynamic.wasm",
    "./large/maelys_datalog_dynamic.wasm": "./large/maelys_datalog_dynamic.wasm"
  },
  "files": ["small/", "large/", "maelys_playground.js", "maelys_playground.d.ts", "README.md", "LICENSE", "licenses/"]
}
EOF
# maelys_playground.d.ts absent aujourd'hui : npm ignore silencieusement les
# entrées "files" manquantes, l'entrée est prête pour le jour où il existera.

# ── Pack ou publish ──────────────────────────────────────────────────────────
if [ "$publish" -eq 1 ]; then
  # Le workflow release calcule le dist-tag et le passe via NPM_DIST_TAG ;
  # en absence (usage local), il est dérivé de la version.
  if [ -n "${NPM_DIST_TAG:-}" ]; then
    npm_tag="$NPM_DIST_TAG"
  else
    case "$version" in
      *-alpha.*|*-beta.*|*-rc.*) npm_tag="next" ;;
      *) npm_tag="latest" ;;
    esac
  fi
  echo "publication npm: @maelys-dev/datalog-wasm@${version} (dist-tag: ${npm_tag})"
  # Registre : publishConfig du package.json ci-dessus. La visibilité du
  # paquet suit celle du dépôt, il n'y a pas d'--access à forcer ici.
  ( cd "$pkg" && npm publish --tag "$npm_tag" )
else
  ( cd "$pkg" && npm pack --pack-destination "$dist" >/dev/null )
  # npm pack nomme le tarball d'après name+version scopés : retrouve-le.
  # npm dérive le nom du tarball du name scopé : @maelys-dev/datalog-wasm
  # donne maelys-dev-datalog-wasm-<version>.tgz.
  produced="$(find "$dist" -maxdepth 1 -name 'maelys-dev-datalog-wasm-*.tgz' | head -1)"
  [ -n "$produced" ] || { echo "erreur: npm pack n'a produit aucun tarball dans $dist" >&2; exit 1; }
  echo "paquet assemblé: ${produced}"
  echo "contenu:"
  tar -tzf "${produced}" | sed 's/^/  /'
fi
