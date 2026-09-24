# Résidus après #110 : un seul run borné

État : protocole préparé, aucune nouvelle mesure lancée. Ne bloque pas 0.10.0.

Le fichier `protocol.json` fixe les révisions, fixtures et 144 passes du diagnostic avant toute observation. Son empreinte est dans `protocol.sha256`. Aucun ordre ne dépendra des résultats. Ces fichiers devront être archivés par le workflow avant la première mesure, avec le commit de l'outillage et les empreintes des harnais.

## Question et périmètre

Vérifier si les deux résidus historiques — SMALL derive/sorted/integer/31 et LARGE solver_size_pure/2048 — varient de façon reproductible sous les placements déclarés. Conserver les six contrôles historiques, sans compter deux fois le contrôle SMALL qui porte aussi le résidu. Ne pas substituer les mesures des pilotes au banc général.

Trois révisions immuables : base A `fe987dcaf98376313d2504a7ed88dff475a96065`, initiale B `2f933260bb127facda4f94e70e192d4cdaa0938c`, corrigée C `1fa69cbd2014915cd53d9d19af28e59ea245a86e`. La comparaison de décision est A/C ; B conserve le témoin du mécanisme d'inlining déjà corrigé. Ce sont les révisions historiques, pas une nouvelle comparaison de #111.

## Exécution

Un seul `bench-compare.yml` manuel, sur un seul job Ubuntu x86-64 séquentiel, sans jeton ni workflow de release. Conserver aussi les matrices générales solveur/entrée/sessions, leurs deux paires A/A puis A B A B ; elles restent la référence. Toutes les compilations, y compris celles des pilotes et des variantes, doivent finir avant le premier chronométrage.

Pour chaque révision/profil, compiler une fois les objets moteur puis relier les mêmes objets avec des remplissages de texte inatteignable de **0, 16, 32, 48, 64, 128 octets**. Vérifier les déplacements réels des symboles dominants, dont `solve_once_derive_ordered`, `maelys_datalog_fact_cmp`, et les fonctions de tri/materialisation présentes. Conserver tous les exécutables, leurs SHA-256, symboles, sections et désassemblages ; refuser une variante dont le déplacement n'est pas celui déclaré.

Le diagnostic comporte trois pilotes : contrôles sessions SMALL, contrôles sessions LARGE, solveur LARGE/2048. Chaque pilote/révision non perturbé reçoit quatre passes A/A avant les deux tours de placement. Le premier tour utilise l'ordre des remplissages `0, 48, 16, 128, 32, 64`, avec rotation de l'ordre A/B/C selon le pilote et le placement. Le second tour inverse exactement le premier. L'inventaire concret est celui de `protocol.json` : 36 passes A/A puis 108 passes de placement.

Garder les fixtures et oracles historiques : 50 échauffements + 301 échantillons pour les sessions ; 500 + 1000 pour le pilote solveur. Mesurer uniquement le solve, vérifier les réponses hors mesure, conserver les échantillons bruts et chaque passe. Aucun réglage d'affinité ou de priorité. Les sélections diagnostiques ci-dessus sont déclarées ; aucun cas ni passe ne sera retiré selon son résultat.

Relever hors mesure les tailles, alignements, offsets et adresses des principales données réellement utilisées par chaque pilote. Ne pas déduire l'alignement absolu d'un offset. Le padding de texte ne contrôle ni les allocations ni la disposition relative de toutes les fonctions entre révisions.

## Comptage et interprétation

Dans des processus distincts du chronométrage, répéter deux fois les comptes Callgrind délimités à chaque solve pour tous les placements. Lire Ir/Dr/Dw exclusifs par fonction, les totaux bruts et le reliquat non attribué. Les échauffements et le contrôle des réponses sont exclus ; ne pas utiliser les compteurs d'appels conservés pendant une collecte désactivée comme dénominateur. Conserver Bcm et I1mr séparément comme événements simulés.

Pour C contre A : `solve_once_derive_ordered` ne doit augmenter ni Ir, ni Dr, ni Dw. Dans les sessions, l'exception historique de l'entrée préparée reste explicitement bornée à +8 Ir/+1 Dr/+2 Dw ; elle n'est pas attendue dans le pilote bas niveau. Toute autre variation est rapportée, sans élargir le critère après mesure. Les répétitions et les comptes stricts d'une même révision doivent rester identiques entre placements.

Sous 10 µs utiliser les minima ; sinon conserver médiane et p95 avec leurs propres planchers A/A. Montrer les classifications par placement, les deux tours séparés et leurs variations au sein de chaque révision. Une amplitude sur ces six placements n'est ni une borne universelle ni une preuve de causalité. Même une sensibilité reproductible ne nomme pas à elle seule un cache ou un prédicteur matériel.

**Compteurs matériels : aucune campagne dédiée.** Le runner peut ne pas les exposer. Archiver uniquement les métadonnées d'accessibilité disponibles sans installation ni modification de permissions ; distinguer « absent/refusé » de « non mesuré ». Aucune conclusion de cycles ou de défauts de cache matériels ne sera tirée de Callgrind.

## Arrêt et livrables

Un seul run hébergé, pas de seconde campagne pour obtenir un résultat plus net. En cas d'échec de l'outillage, conserver les artefacts partiels et rapporter que l'expérience n'est pas concluante ; ne pas relancer implicitement.

Livrer le protocole archivé, les binaires et empreintes, les données de disposition, tous les CSV, les comptes et un rapport qui conserve les observations historiques +6,44 % et +10,38 %. Si aucun mécanisme n'est isolé, ouvrir une modification documentaire distincte d'AGENTS.md pour consigner cette limite et arrêter l'investigation. Aucun changement moteur ne sera déduit du chronométrage seul. Cette investigation ne conditionne pas la coupe de 0.10.0.
