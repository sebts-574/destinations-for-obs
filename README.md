# Multidestination Studio

Plugin OBS Studio qui ajoute des **destinations** : des sorties indépendantes,
chacune avec ses scènes, son Preview/Program et ses transitions, à la manière
des surfaces de QLab.

> Statut : **V0.9.3 — prototype**. Compile pour macOS, Windows et Linux ;
> la V0.8 a été testée dans OBS (sauf l'Output blending), la V0.9 pas encore.

## Installer

Un seul fichier par système, dans la Release GitHub (ou l'onglet Actions →
build → Artifacts) :

- **Windows** : `multidestination-studio-x.y.z-windows-x64-installer.exe`.
  Installe dans `C:\ProgramData\obs-studio\plugins\multidestination-studio`
  (textes compris) et retire les anciennes copies manuelles mal placées.
- **macOS** : `multidestination-studio-x.y.z-macos-universal.pkg` (non signé :
  Réglages Système → Confidentialité et sécurité → Ouvrir quand même).
- **Linux** : `.deb` (`sudo apt install ./fichier.deb`).

Fermer OBS avant d'installer.

## Nouveautés V0.9.3

- Noms des destinations jamais abrégés : la police rétrécit pour que tout
  le nom tienne.
- Titres Aperçu / Programme replacés et dimensionnés d'après la maquette.
- Disposition des docks par défaut : Scènes, Sources, Contrôles média,
  Mixeur audio, Transitions de scène, Commandes, côte à côte en bas.
  « Réinitialiser les docks » dans le menu Docks.
- Info F11 : « pour basculer en plein écran » ou « pour quitter le plein
  écran » selon l'état de la fenêtre.

## Nouveautés V0.9.2

- Installeur Windows : le plugin s'installait dans l'ancien dossier
  « destinations-for-obs » (repris de l'installation précédente), qu'OBS
  ignore puisque le plugin s'appelle maintenant multidestination-studio. Il
  s'installe désormais toujours dans `plugins\multidestination-studio`.

## Nouveautés V0.9.1

- Langue du plugin indépendante de celle d'OBS : Configuration Settings →
  Langue… (comme OBS, English, Français, Norsk). Prise en compte au
  redémarrage d'OBS.
- Le journal d'OBS indique la langue d'OBS et celle du plugin.

## Nouveautés V0.9

- Le projet s'appelle **multidestination-studio** (fichiers, plugin,
  installeurs). Les destinations et réglages déjà enregistrés sont gardés.
  L'installeur Windows supprime l'ancien plugin « destinations-for-obs » ;
  sur macOS et Linux, désinstaller l'ancien à la main pour qu'OBS ne charge
  pas les deux.
- Interface traduite en norvégien (bokmål), avec les termes d'OBS.
- Info « F11 pour basculer en plein écran » à côté des menus.
- Destination absente de la scène : image hachurée barrée d'une croix et
  badge rouge « Absente de cette scène ».
- Noms des destinations en gras, taille adaptée à la colonne, abrégés si
  besoin. Titres Aperçu / Programme encore plus grands.

## Nouveautés V0.8

- Les destinations sont gardées dans la collection de scènes même quand
  OBS enregistre pendant sa fermeture (elles étaient perdues au
  redémarrage).
- Multidestination Studio s'ouvre en plein écran. Souris en haut à droite :
  « Réduire », « Quitter le plein écran », « Quitter ». F11 bascule le
  plein écran.
- Sélection de plusieurs scènes ou sources par cliquer-glisser (Maj / Ctrl
  aussi). Pour déplacer, cliquer-glisser depuis un élément déjà
  sélectionné.
- Suppr supprime les scènes ou sources sélectionnées, après confirmation.
- Réglages par défaut : « L'ancien Programme revient en Aperçu » et
  « Toutes les destinations, en permanence », marqués « (recommandé) ».
- Boutons Agrandir / Verrouiller dimensionnés d'après leur texte, en-tête
  plus haut sur les grandes colonnes ; titres Aperçu / Programme en grand
  et en gras ; « Modifier le PGM » détaché de Transition / Cut ; textes
  d'aide en blanc.

## Nouveautés V0.7

- Ordre des scènes et des sources : glisser-déposer dans les listes,
  boutons ↑ / ↓ et menu clic droit (monter, descendre, en haut, en bas).
  L'ordre des scènes est celui de la liste de scènes d'OBS.
- Nouveau menu « Configuration Settings », à côté de Docks :
  - « Après une transition… » : l'ancien Programme revient en Aperçu (comme
    OBS), ou la scène passée à l'antenne reste en Aperçu ;
  - « Destinations affichées… » : toutes les destinations en permanence
    (une destination absente de la scène a un Aperçu vide), ou seulement
    celles de la scène sélectionnée.
  Les réglages sont gardés dans la configuration d'OBS.
- Boutons Agrandir / Verrouiller : taille du texte adaptée à la largeur,
  symbole seul quand la place manque.
- Titres « Aperçu » et « Programme » au-dessus des destinations.

## Nouveautés V0.6

- Une vidéo inactive (pas lue, sans image) se déplace, se redimensionne et se
  modifie comme les autres sources, y compris juste après l'ajout. Sa taille
  d'image est mémorisée dès qu'elle a été vue une fois, et enregistrée avec
  la collection. Une vidéo jamais vue garde un cadre provisoire ; si on le
  redimensionne, l'image s'y adaptera en gardant ses proportions.

## Nouveautés V0.5

- **Output blending** (menu Destinations → « Output blending… ») : une
  destination répartie sur plusieurs sorties (vidéoprojecteurs).
  - « Nouvelle sortie », chacune avec son écran ; on la glisse sur le layout.
    Elle garde la forme de son écran et ne peut pas sortir du layout.
  - Taille du layout réglable dès la première sortie ; les sorties suivent.
  - Édition à la souris comme en Aperçu/Programme (aimant au centre, Maj =
    ratio libre, flèches, Suppr, Ctrl+Z).
  - Fondus automatiques sur les recouvrements (ou manuels), gamma, courbe,
    niveau de noir, corner pin, mire.
  - « Save & Create destination » ; ensuite « Output blending… » dans le
    sous-menu ou les paramètres de la destination, avec « Save changes »
    (grisé une fois enregistré) et « Close » (demande si non enregistré).
  - « Ouvrir les sorties » : chaque sortie en plein écran sur son écran, les
    réglages s'affichent en direct pendant l'édition.
- Les retouches faites avec « Modifier le PGM » sont gardées dans la scène.
- Les fichiers importés gardent leur taille d'origine, centrés.
- Correction du décalage Aperçu / Programme sur les grandes destinations.
- Les contours (destination, source) s'effacent quand on clique ailleurs.

## Nouveautés V0.4

- **Modifier le PGM** (au-dessus de Transition, rouge quand activé) : par
  défaut, ce qui est à l'antenne est une copie de la scène ; les retouches
  faites en Aperçu ne passent à l'antenne qu'à la transition suivante.
  Activé, le Programme devient éditable directement.
- **Agrandir** une destination (Aperçu + Programme) pour placer finement.
- Édition : proportions gardées sur toutes les poignées (Maj pour les
  libérer), aimant seulement au centre, débordement autorisé avec contour
  orange, contours et guides verts, guides seulement sur la destination
  active, contour vert discret autour de la destination de travail.
- **Annuler** (Ctrl+Z).
- **Glisser-déposer** de fichiers (images, vidéos, sons, pages HTML).
- **Contrôles média** (lecture, pause, arrêt, position) ; un média ajouté a
  tout de suite un cadre à placer, et s'arrête sur sa première image.
- **Mixeur** : sources actives puis celles de la scène sélectionnée,
  « Tout afficher », écoute (monitoring) par source.
- **Destination par défaut** : « Désactiver et masquer » (menu
  Destinations) ; ses scènes sont mémorisées.
- **Nouvelle destination** : case « Ajouter aux scènes existantes »
  (décochée par défaut).

## Multidestination Studio (V0.3)

- **Ouverture** : bouton « Multidestination Studio » dans le dock Commandes
  d'OBS (entre « Mode Studio » et « Paramètres »), menu Outils, ou raccourci.
  Échap ou Fichier → Quitter pour revenir à OBS.
- **Fenêtre façon OBS** : Aperçu / Programme au centre (destinations de la
  scène sélectionnée, empilées), boutons Transition et Cut entre les deux ;
  docks Scènes, Sources, Mixeur audio, Transitions de scène, Commandes
  (déplaçables, menu Docks).
- **Scènes** : chaque nouvelle scène (ici ou dans OBS) demande sur quelles
  destinations l'afficher, toutes cochées par défaut. Chaque destination a sa
  mise en page de la scène.
- **Sources** : création de nouvelles sources directement dans la vue (même
  liste de types que le « + » d'OBS), ajout de sources existantes ou de
  scènes.
- **Édition à la souris** : déplacer, redimensionner (Maj : proportions
  libres), recadrer (Alt), aimantation (Ctrl pour la couper), guides quand la
  source est centrée.
- **Raccourcis** (dans les aperçus et la liste des sources) : Suppr/Retour
  arrière, Ctrl+C / Ctrl+V, Ctrl+Maj+C / Ctrl+Maj+V (transformation), Ctrl+R,
  Ctrl+F, Ctrl+S, Ctrl+D, Ctrl+↑ / ↓ / Début / Fin, flèches (Maj : ×10).
  Cmd au lieu de Ctrl sur macOS.
- **Transition** : une seule transition, celle d'OBS (type, durée,
  propriétés), appliquée à toutes les destinations. Après une transition,
  l'ancien Programme revient en Aperçu, comme dans OBS. Une destination que
  la scène n'affiche pas garde son Programme ; **Verrouiller** exclut une
  destination.
- **Menu Destinations** : chaque destination (Paramètres, Ouvrir en plein
  écran sur son écran de sortie, Ouvrir en fenêtre, Fermer la projection,
  Supprimer avec confirmation) et « Nouvelle destination… » (nom,
  résolution, écran de sortie). Une nouvelle destination est ajoutée à toutes
  les scènes existantes.

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
| V0.3 | Multidestination Studio façon OBS, sources, mixeur, transition globale, menu Destinations |
| V0.4 | Modifier le PGM, agrandir, annuler, glisser-déposer, médias, mixeur, installeurs |
| V0.5 | Output blending : sorties, fondus (courbe + gamma), niveau de noir, corner pin, mire |
| Plus tard | Sorties DeckLink / NDI par sortie ; stream et enregistrement par destination |

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
