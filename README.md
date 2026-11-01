# Destinations for OBS

Plugin OBS Studio qui ajoute des **destinations** : des sorties indépendantes,
chacune avec ses scènes, son Preview/Program et ses transitions, à la manière
des surfaces de QLab. Nom de projet provisoire.

> Statut : **V0.2 — prototype**. Compile pour macOS, Windows et Linux ;
> la V0.1 a été testée dans OBS, la V0.2 pas encore.

## Ce que fait la V0.2

- **Scènes communes** : une scène OBS couvre une ou plusieurs destinations,
  choisies à sa création (y compris quand on crée la scène depuis OBS).
  Chaque destination a sa propre version de la scène : sources et placement.
- **Destination par défaut** = le canvas principal d'OBS (Paramètres → Vidéo),
  avec son stream, son enregistrement et sa transition.
- **Vue Studio** (menu Outils, barre d'outils « Vue Studio », raccourci ;
  Échap pour quitter) : PREVIEW et PROGRAM côte à côte, avec seulement les
  destinations couvertes par la scène sélectionnée, empilées verticalement.
- **Édition à la souris** dans le PREVIEW : sélectionner, déplacer
  (aimantation aux bords et au centre, Ctrl pour la couper), redimensionner
  (Maj pour libérer les proportions), recadrer (Alt + poignée), flèches pour
  décaler, Suppr pour retirer, clic droit pour ajuster / étirer / ordre /
  propriétés / filtres.
- **TAKE global** (AUTO ou CUT) : chaque destination couverte bascule avec sa
  propre transition. **Verrou** par destination pour qu'elle ignore le TAKE
  global, et **TAKE** individuel.
- **Suivre les scènes d'OBS** (option) : un changement de scène fait dans OBS
  entraîne les autres destinations.
- Plusieurs destinations, chacune avec sa résolution, sa transition et son
  projecteur plein écran.

## Compatibilité

- OBS Studio **31.1 ou plus récent** (API des canvases).
- macOS (Apple Silicon + Intel), Windows x64, Ubuntu 24.04 x86_64.

## Compiler

Rien à installer : chaque `git push` sur `main` lance GitHub Actions, qui
compile pour les trois systèmes. Les fichiers sont dans l'onglet **Actions**
→ build → *Artifacts*. Un tag `0.1.0` (sans « v ») crée une Release GitHub en brouillon.

## Décisions, hypothèses, points ouverts

**Décisions (V0.1)**
- Plugin OBS, pas de fork. Base : `obs-plugintemplate` officiel.
- Le canvas de la destination ne mixe pas l'audio (flags `ACTIVATE | SCENE_REF`) :
  l'audio reste géré par le mixeur principal d'OBS, pour éviter les doublons.
- La transition Stinger est masquée tant qu'il n'y a pas de fenêtre de réglages.
- Vérification du format du code (clang-format, gersemi) retirée de la CI
  pour ne pas bloquer les builds.

**Hypothèses à vérifier au premier test**
- OBS sauvegarde et recharge le canvas et ses scènes avec la collection.
- L'état propre au plugin (Preview, Program, transition) est relu avant
  l'événement « chargement terminé ».
- La sortie Program affichée est bien la texture finale du canvas.

**Points ouverts**
- Nom définitif du plugin.
- Édition des sources à la souris (position, recadrage) : V0.2 ou plus tard.

## Feuille de route

| Version | Contenu |
|---|---|
| V0.1 | Une destination : scènes, PVW/PGM, transitions, projecteur |
| V0.2 | Scènes communes, vue Studio, édition à la souris, TAKE global, N destinations |
| V0.3 | Layout : tranches, edge blending (courbe + gamma), corner pin |
| V0.4 | Sorties par tranche : DeckLink, NDI ; stream et enregistrement |

## Arborescence

```
src/destination.*       moteur : canvas, scènes, PVW/PGM, transitions (sans Qt)
src/destination-dock.*  interface du dock
src/projector-window.*  projecteur plein écran
src/plugin-main.cpp     chargement du plugin, sauvegarde, événements OBS
src/qt-display.*        widget d'affichage OBS (repris d'OBS Studio)
data/locale/            textes de l'interface (en-US, fr-FR)
.github/workflows/      compilation automatique macOS / Windows / Linux
buildspec.json          nom, version, version d'OBS ciblée
```

## Licence

GPL-2.0-or-later (obligatoire pour un plugin OBS). `src/qt-display.*` et
`src/display-helpers.hpp` sont repris d'OBS Studio (via Aitum Vertical).
