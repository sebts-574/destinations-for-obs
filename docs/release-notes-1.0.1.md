# Multidestination Studio 1.0.1

Bug-fix release. Updating from 1.0.0 is recommended for everyone.

## Fixed
- **Destinations are kept when OBS restarts.** At startup OBS reports the scene collection as loaded twice; the second time, the plugin dropped the destinations it had just loaded, so they were missing from the next save. Destinations now load once and stay.
- **Lost destinations are recovered.** Destinations that disappeared with earlier versions are found again from the canvases OBS kept, with their scenes and layouts. Check their resolution in Destinations → Settings, as it could not be recovered.
- **"Add to existing scenes" is respected.** A new destination could reuse the canvas of a previous destination with the same name, and inherit all its scenes. New destinations now always get their own canvas (a number is added to the name if needed).

## Improved
- French and Norwegian: the remaining English labels are translated (output blending, corner pin, settings menu, startup setting, save and discard buttons).
