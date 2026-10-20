# Helm HUD artwork

The throttle housing, movable grip, radar bezel and Console background are exact copies of the original PNGs with equivalent sprite sidecars in `raw/`. Originals are unchanged. Runtime uses the housing's travel markers and the background's nine-slice dimensions.

`readout-panel.png` and `heading-pointer.png` were generated with the built-in image_gen tool using the existing artwork as style references. Both retain transparency. Their exact prompts are in `generation.json`. All labels, requested values and actual values remain live UI text. The external On screen control reuses the readout housing and a cropped section of its ivory rail for its two mounting arms; it keeps the same illustrated material treatment as the radar and throttle.

Artwork supplies appearance only. The authorised Console model owns control admission; authored widget bindings select controls. Mouse/touch heading selection uses the bezel's annulus, and keyboard/screen-reader users can edit the same desired heading numerically.

`camera-direction.png` and `camera-mode.png` are transparent image_gen faceplates in the same style. The octagonal selector has eight inactive pads around a ship silhouette; the rocker has two inactive faces. Their prompts and normalized pad centers are recorded in `camera-generation.json`. RmlUi overlays live arrows, 1P/3P labels, accepted-state highlights, focus borders and localized status text. The same artwork appears on authored camera widgets on any Console and in Workshop preview; it never owns camera state or admission.

`manual-toggle.png` is the single switch housing mounted on the radar's lower-left rim. Its prompt is in `manual-toggle-generation.json`. Runtime supplies the Manual label, pressed state and lamp: a filled green circle with a plus and On, or an outlined red square with a minus and Off. Shape, symbol and text carry the state alongside color. The two former profile buttons are replaced by this one admitted control-profile toggle; detailed profile selection remains in Settings.
