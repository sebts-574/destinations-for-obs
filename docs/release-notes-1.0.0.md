# Multidestination Studio 1.0.0

First stable release of Multidestination Studio, the OBS Studio plugin for running several independent video destinations from one full-screen studio view.

## What changed since 0.5.0

### Fixed
- **Destinations are no longer lost when OBS restarts.** OBS saves the scene collection while it closes; the plugin now always writes the last known destinations instead of an empty list.
- **Windows installer.** The plugin always installs to `plugins\multidestination-studio`. Leftovers from earlier test builds, including the old `destinations-for-obs` folder, are removed so OBS never loads two copies.
- **Inactive videos can be edited.** A video that is not playing can be moved, resized, cropped, fitted and centred like any other source. Its picture size is remembered with the scene collection.
- **"&" in button labels** is displayed correctly.

### New
- **Configuration Settings menu**
  - *After a transition*: the previous Program returns to Preview (recommended, default), or the scene just taken stays in Preview.
  - *Destinations shown*: all destinations all the time (recommended, default), or only the destinations of the selected scene.
  - *Startup Settings*: "Launch Multidestination Studio on startup" switch (off by default).
  - *Language*: same as OBS, English, Français or Norsk (Bokmål), independent of the OBS language.
- **Norwegian (Bokmål) translation**, using the OBS terminology.
- **Full-screen studio**: opens full screen; Minimize, Exit fullscreen and Quit appear when the mouse reaches the top-right corner; F11 toggles full screen, with a hint next to the menus.
- **Scene and source order**: drag and drop, ↑ / ↓ buttons and right-click commands. Scene order is the OBS scene order.
- **Multi-selection** of scenes or sources by click-and-drag, Shift or Ctrl.
- **Delete key** removes the selected scenes or sources, after confirmation.
- **Absent destinations are obvious**: when a scene does not cover a destination, its Preview is hatched, framed in red and labelled "Not in this scene".
- **Reset docks** in the Docks menu, back to the default bottom layout.

### Improved
- Destination names are never shortened: the text scales to fit.
- Enlarge and Lock buttons size themselves to their text and switch to icons when space is short.
- Larger, bold Preview and Program titles that scale with the window.
- "Modify PGM" sits apart from Transition and Cut.
- Help texts in dialogs are easier to read on dark themes.
- The project, plugin and packages are now named **multidestination-studio**.

## Features in 1.0.0

### Destinations
- The OBS main canvas is the default destination; add as many extra destinations as you need, each with its own canvas, name, resolution and output screen.
- Open a destination fullscreen on its output screen or in a window; Esc closes it.
- The default destination can be deactivated and hidden (its scenes are kept; streaming and recording are disabled meanwhile).
- Everything is saved in the OBS scene collection.

### Shared scenes
- One scene list for all destinations. When a scene is created, from the studio or from OBS, you choose the destinations it covers.
- Each destination keeps its own version of every scene it covers, with its own sources and layout.

### Multidestination Studio view
- Full-screen workspace in the OBS style, opened from the Controls dock, the Tools menu or a hotkey.
- Preview column (left) and Program column (right), one tile per destination, stacked and scaled to the window. Enlarge a single destination for precise work.
- Docks: Scenes, Sources, Media controls, Audio mixer, Scene transitions, Controls.

### Going live
- Transition and Cut send every unlocked destination covered by the scene to Program with the OBS transition and duration.
- Lock a destination to keep its Program untouched.
- Hotkeys for open/close, Transition and Cut.
- What is on air is a copy of the scene: Preview edits never reach the live output. Modify PGM edits the live picture directly and keeps those edits in the scene.

### Editing sources
- Move, resize (aspect kept, Shift frees it), crop (Alt), centre snapping (Ctrl disables it), arrow-key nudging.
- Undo (Ctrl+Z), copy and paste, copy and paste transform, fit, stretch, centre, reset, order, properties, filters.
- Drop image and video files onto a destination: they are added at their native size, centred.
- A source outside the frame is outlined in orange.

### Output blending
- Split one destination across several projectors. Each output has its own screen and is placed on a layout of any size, keeping its screen's shape.
- Overlaps are detected and blended automatically, or set edge by edge.
- Per-output gamma, blend curve, black level and corner pin; built-in test pattern.
- Live preview on the projectors while adjusting; changes are saved explicitly, with a prompt for unsaved changes.

### Audio and media
- Audio mixer with meters, faders, mute and per-source monitoring; shows the active sources and the selected scene, or all sources.
- Play / pause, restart and stop for media sources.

### Languages
English, French and Norwegian (Bokmål).

## Installation
- **Windows 10/11**: `multidestination-studio-1.0.0-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `.deb` package (`sudo apt install ./file.deb`).

Close OBS before installing. Requires OBS Studio 31.1 or later.
