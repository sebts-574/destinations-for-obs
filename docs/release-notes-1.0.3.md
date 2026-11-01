# Multidestination Studio 1.0.3

Bug-fix release with a new destination manager. Updating from 1.0.0 is recommended for everyone.

## Fixed
- **Destinations are kept when OBS restarts.** At startup OBS reports the scene collection as loaded twice; the second time, the plugin dropped the destinations it had just loaded, so they were missing from the next save. Destinations now load once and stay.
- **Lost destinations are recovered.** Destinations that disappeared with earlier versions are found again from the canvases OBS kept, with their scenes and layouts. Check their resolution in Destinations → Manage destinations, as it could not be recovered.
- **"Add to existing scenes" is respected.** A new destination could reuse the canvas of a previous destination with the same name, and inherit all its scenes. New destinations now always get their own canvas (a number is added to the name if needed).

## New
- **Destinations → Manage destinations…**: every destination in one window. For each one: name, resolution, output screen, lock, and the list of scenes it appears in, which can be ticked or unticked directly. Output blending, fullscreen projection and delete (or deactivate, for the default destination) are one click away, and new destinations can be created from the same window.

## Improved
- When "All destinations, all the time" is on, the New destination dialog explains that the new destination will be visible under every scene, and shows "Not in this scene" wherever it was not added.
- A destination that the selected scene does not cover is easier to spot: lighter hatching and a thicker red frame around "Not in this scene".
- The OBS log records whether a new destination was added to the existing scenes.
- French and Norwegian: the remaining English labels are translated (output blending, corner pin, settings menu, startup setting, save and discard buttons).
