# Destinations for OBS

Plugin OBS Studio qui ajoute des **destinations** : des sorties indépendantes,
chacune avec ses scènes, son Preview/Program et ses transitions, à la manière
des surfaces de QLab. Nom de projet provisoire.

> Statut : **V0.1 — prototype non testé dans OBS**. Le code compile sans
> avertissement contre les en-têtes d'OBS 31.1.1 et de Qt 6.8, mais il n'a pas
> encore tourné dans OBS. Une seule destination. Pas encore de blending, ni de
> sorties DeckLink / NDI / stream propres à la destination.

## Ce que fait la V0.1

- Un dock **« Destination 1 »** dans OBS (menu *Docks*).
- Un canvas OBS dédié, en 1920×1080 par défaut (bouton *Résolution…*), à la
  même cadence que le canvas principal.
- Des scènes propres à la destination (boutons + / −).
- Dans chaque scène : ajout de **sources OBS existantes** (caméras, NDI,
  médias, navigateurs…) ou de **scènes du canvas principal**. La source est
  ajustée au cadre (pas encore d'édition à la souris).
- **PREVIEW / PROGRAM** côte à côte, avec tally (vert / rouge) dans la liste.
- Transitions : type (Cut, Fondu, Glissement, Balayage…), durée, boutons
  **CUT** et **AUTO**.
- **Projecteur** : sortie Program en plein écran sur l'écran choisi
  (Échap pour fermer, double-clic pour passer en fenêtre), ou en fenêtre.
- Sauvegarde dans la collection de scènes OBS.

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
| V0.2 | N destinations, sélecteur, raccourcis clavier, réglages des transitions |
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
