# Multidestination Studio 1.0.4

## Fixed
- **macOS: the Destinations menu is displayed.** The menu is now built in advance instead of when it opens, which native macOS menus did not show reliably. Rebuilding is delayed slightly so that a command chosen in the menu (such as Delete) never removes the menu item it was started from.
- **macOS: output screens stay visible.** Destinations open as borderless windows covering their screen instead of native full screen, which created a separate Space, hid the other screens and disappeared when you clicked in OBS. Double-clicking an output switches between window and full-screen size correctly on every screen.

## New
- **Paste (duplicate)** (Edit menu and right-click, Ctrl+Alt+V): pastes an independent copy of the copied source, with its filters, under a new name. **Paste (reference)** (Ctrl+V) still adds the same source.
- **Startup Settings → Open every destination on its output screen at startup** (off by default): when OBS starts, every destination that has an output screen opens on it, including each projector of a blended destination.
