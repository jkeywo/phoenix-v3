# Menu title and touch maps

20 October 2026, Linux/Xvfb and Chromium with software rendering.

The HTML title animation reports 3,000 ms. At 1280 × 720 its horizontal transform is 640 px at entry, −168 px at 2,400 ms, and −150 px at completion: an 18 px overshoot followed by correction. The title's stacking level is −1 and the cockpit frame's is 0. Native presentation uses the same duration and overshoot, with a separate frame element above the title. Both paths retain immediate centering for reduced motion.

A browser Workshop run used actual emulated touch events through the production canvas input route. An 80 × 40 px one-finger drag moved the map's player-slot marker by the same amount. A two-finger pinch then expanded the grid around the moving gesture midpoint. The three captures are in ignored local output, `build/web/title-touch-smoke/workshop-{before,pan,pinch}.png`. The GM inspection map uses the same gesture controller with its existing zoom bounds; a live remote GM journey was not exercised in this pass.

The synthetic input check covers one-finger pan, anchored pinch, zoom limits, cancellation, returning to one finger, capture outside the map, and refusing touches that begin outside it. All 22 native CTests and 10 HTML launcher cases pass. The Linux game and all three browser role builds pass. The Linux Workshop graphical smoke passes; this environment has no audio playback device. Physical touchscreen hardware remains unverified.
