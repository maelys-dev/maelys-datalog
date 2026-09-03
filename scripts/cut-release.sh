#!/usr/bin/env bash
#
# Coupe une release en une commande. Depuis un arbre propre et une main à
# jour, ce script :
#   1. écrit VERSION — source de vérité unique — et régénère
#      include/maelys_datalog_version.h à partir de lui
#      (scripts/generate-version-header.sh) ;
#   2. lance `make check` EN LOCAL, puis `make check CC=gcc` dans un
#      conteneur Linux — la porte qui détecte un bump cassé, sur les deux
#      compilateurs réellement utilisés pour ce projet, avant que ça
#      n'atteigne la CI ou un tag ;
#   3. ouvre une PR de release et attend les checks requis ;
#   4. fusionne la PR, puis crée et pousse le tag annoté vX.Y.Z[-alpha.N].
#
# Le tag déclenche ensuite .github/workflows/release.yml, qui construit les
# artefacts natifs et WASM et attend ton approbation sur l'environnement
# `release` avant de les publier (voir docs/release-engineering.md).
#
# Usage: scripts/cut-release.sh X.Y.Z[-alpha.N] [--skip-container]
#   Écris d'abord l'entrée CHANGELOG.md : `## [X.Y.Z] - <date>`.
#
#   --skip-container  saute la porte GCC/Linux en conteneur (voir plus bas).
#                      À n'utiliser qu'en connaissance de cause : ce n'est
#                      jamais le comportement par défaut, et le script le dit
#                      bruyamment quand tu l'actives.
#
# Modèle : porté depuis mcp-runtime/scripts/cut-release.sh (voir
# docs/release-engineering.md, invariant 3). Adaptations propres à ce dépôt :
#   - le regex de version accepte les pré-releases alpha (D4) ;
#   - la porte CHANGELOG cherche le format Keep-a-Changelog avec crochets
#     (`## [X.Y.Z] - <date>`), pas `## X.Y.Z - <date>` ;
#   - le conteneur de la seconde porte n'installe que build-essential : le
#     moteur n'a aucune dépendance tierce à l'exécution (D1) — pas de
#     jansson/uriparser à installer comme côté mcp-runtime ;
#   - pas d'étape Homebrew ici : tap conditionnel, voir D5, ajout ultérieur ;
#   - le commit de bump ne contient que VERSION + le header généré, jamais
#     CHANGELOG.md (l'entrée doit déjà être sur main avant de lancer ceci).
set -euo pipefail

repo_slug="maelys-dev/maelys-datalog"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

# --- lecture des arguments : une version positionnelle, un flag optionnel ---
ver=""
skip_container=0
for arg in "$@"; do
  case "$arg" in
    --skip-container)
      skip_container=1
      ;;
    -*)
      echo "usage: $0 X.Y.Z[-alpha.N] [--skip-container]" >&2
      exit 1
      ;;
    *)
      if [ -n "$ver" ]; then
        echo "usage: $0 X.Y.Z[-alpha.N] [--skip-container]" >&2
        exit 1
      fi
      ver="$arg"
      ;;
  esac
done

if ! [[ "$ver" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-alpha\.[0-9]+)?$ ]]; then
  echo "usage: $0 X.Y.Z[-alpha.N] [--skip-container]  (SemVer, alpha pré-release optionnelle — D4)" >&2
  exit 1
fi
tag="v${ver}"

# --- préconditions : arbre propre, sur main, à jour, tag libre, notes écrites ---
[ -z "$(git status --porcelain)" ] || { echo "arbre de travail non propre" >&2; exit 1; }
git fetch origin --quiet
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] \
  || { echo "HEAD doit être égal à origin/main — fais 'git switch main && git pull' d'abord" >&2; exit 1; }
if git ls-remote --exit-code --tags origin "$tag" >/dev/null 2>&1; then
  echo "le tag $tag existe déjà sur origin" >&2; exit 1
fi
grep -q "^## \[${ver}\] - " CHANGELOG.md \
  || { echo "CHANGELOG.md n'a pas d'entrée '## [${ver}] - <date>' — écris les notes d'abord" >&2; exit 1; }

# --- VERSION est la source de vérité unique ; régénère le header à partir d'elle ---
printf '%s\n' "$ver" > VERSION
bash scripts/generate-version-header.sh

# --- porte locale : ne jamais taguer ce que make check rejette ---
echo "==> make check (porte locale)"
make clean >/dev/null
make check

# --- seconde porte locale : le compilateur réellement utilisé par la CI.
# Le moteur n'a aucune dépendance tierce à l'exécution (D1, yyjson est
# vendorisé) : pas de cmake/curl/jansson/uriparser à installer ici, à la
# différence du modèle mcp-runtime — build-essential (gcc + make) suffit.
# Même motif que le modèle : image pinnée, copie jetable du source, jamais
# de build in-place sur le montage en lecture seule. ---
if [ "$skip_container" -eq 1 ]; then
  echo "ATTENTION : --skip-container fourni — la porte GCC/Linux en conteneur est IGNORÉE." >&2
  echo "            Ce n'est jamais le comportement par défaut ; à n'utiliser qu'en connaissance de cause." >&2
elif ! command -v docker >/dev/null 2>&1; then
  echo "docker introuvable — requis pour la porte GCC/Linux (voir docs/release-engineering.md)." >&2
  echo "Installe/démarre Docker, ou relance avec --skip-container si tu assumes le risque." >&2
  exit 1
else
  echo "==> make check CC=gcc, dans un conteneur Linux (seconde porte locale)"
  docker run --rm --platform linux/arm64 -v "$root:/source:ro" ubuntu:24.04 bash -c '
    set -e
    apt-get update -qq && apt-get install -y -qq --no-install-recommends build-essential
    cp -a /source /work && cd /work && rm -rf build
    make check CC=gcc
  '
fi

# --- branche, commit, PR ---
branch="release/${tag}"
git switch -c "$branch"
git add VERSION include/maelys_datalog_version.h
git commit -q -m "release: ${ver}"
git push -u origin "$branch"
gh pr create --repo "$repo_slug" --base main --head "$branch" \
  --title "release: ${ver}" --body "Bump de version automatisé pour ${tag}."

# --- attente des checks requis (échoue si rouge), puis fusion ---
echo "==> attente des checks requis…"
gh pr checks --repo "$repo_slug" "$branch" --watch
gh pr merge --repo "$repo_slug" "$branch" --squash --delete-branch

# --- tag du commit de merge sur main, et push ---
git switch main
git fetch origin --quiet
git tag -a "$tag" origin/main -m "maelys-datalog ${ver}"
git push origin "$tag"

echo "==> ${tag} poussé."
echo "    Le workflow Release construit les artefacts ; approuve le job"
echo "    'publish' quand il affiche 'Review required' pour les attacher à"
echo "    la GitHub Release et publier le paquet npm (voir D5)."

# --- tap Homebrew (D5) : bump automatique en dernière étape. La formule
# construit depuis l'archive source du tag — elle n'attend ni le workflow
# binaire ni son approbation, exactement comme côté mcp-runtime. ---
echo "==> bump du tap Homebrew"
bash scripts/update-tap-formula.sh "$tag"
