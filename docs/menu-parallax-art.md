# Main-menu parallax artwork

Generated and edited with the built-in imagegen tool. Assets are saved under `content/ui/menu/`: `menu-debris.png` and `menu-logotype.png`. Exact image dimensions are recorded in `menu-scene.json`.

## Final prompts

Debris: Gritty charcoal asteroids and rusty ivory naval wreckage with dim upper-left highlights. Three uneven loose groups with large gaps. Angle the left small group bottom-left to top-right, and the middle group top-left to bottom-right. Scatter perimeter pieces outward at varied distances to make edges irregular and porous. Then halve every individual piece's width and height, retain the canvas size and approximate piece count, and scatter them much more loosely and randomly from left to right. Preserve uneven density and large empty gaps in a narrow mid-height band. No background, haze, stars, glow, lettering or annotations; true transparency.

Logotype: Exact wording "Against" above "the Stars", tucked together in heavy blackletter, distressed ivory/rust and black ink outlines. Small flourishes at upper-left and lower-right. All letters solid and continuous, with no laser cuts. A long fine thruster trail runs through the gap between rows, with a tiny right-facing naval spaceship at its far right end. Dirty ivory/rust ship, tiny orange engine glow and slender pale warm exhaust. Ship and trail sit behind all lettering and flourishes, which occlude them. Taper the trail smoothly leftwards: widest and brightest at the engine, progressively narrower and fainter, ending at a near-invisible point. Transparent background.

## Motion

Both renderers keep debris at 1.5 times the panorama speed, with a faint warm rim driven by the existing flash envelope. The title enters from the right over three seconds, overshoots its centred position by 18 pixels, then settles back. It passes behind the cockpit frame. Reduced motion pauses scenery, disables flashes and centres the title immediately. Title timing is independent of the panorama speed.

## Verification

Native presentation and all three web role builds pass. Ten browser-launcher tests and the native menu-library/input-and-semantics integration checks pass. Browser preview confirms left-aligned initial buttons, title arrival and landscape rotation. Native menu smoke captures the new menu and proceeds into gameplay/settings, but the full journey reaches its 30-second timeout; this is not recorded as a full smoke pass.
