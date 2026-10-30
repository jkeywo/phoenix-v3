# Bullfrog sprite sheets

Three transparent PNG atlases, extracted with Pillow, NumPy and SciPy from the five latest approved drawings. No ImageGen was used for this extraction. Retained RGB pixels are copied unchanged: no resizing, recolouring, filtering, rotation or inpainting. Only alpha coverage and integer placement change.

| Sheet                        | Dimensions  | Five stored regions |
| ---------------------------- | ----------- | ------------------- |
| `bullfrog-hull.png`          | 7680 × 1280 | 1536 × 1280 each    |
| `bullfrog-top-turret.png`    | 2560 × 256  | 512 × 256 each      |
| `bullfrog-bottom-turret.png` | 2560 × 256  | 512 × 256 each      |

The order is bow, port bow, port, port stern, stern. Each matching `.sprite.json` uses the repository's schema-1 source sidecar contract, with five unique rectangles and eight explicit bearings. Starboard bearings reuse the three port drawings through `mirror_x`, leaving the PNG pixels untouched. Rectangles, pivots and markers remain in unmirrored frame-local coordinates.

The hull's `turret_top` and `turret_bottom` markers align with the corresponding turret pivots. Turret `render_scale` is 0.2 relative to hull scale 1, matching their 256/1280 frame heights, so one source pixel has the same rendered size across all three sheets. Scale 1 is an authoring reference, not a chosen ship size in metres. Source drawings have not been resampled to harmonise their apparent sizes between views.

The fixed shoulder cannon and torpedo remain on the hull. Only the visible roof and belly turret pixels have been separated. Covered surfaces do not exist in the source images: transparent cut-outs remain at the mounts, and hidden portions of the turrets have not been invented. The layers reconstruct the original visible artwork at their authored pivots; independently traversing turrets may expose those gaps. No depth or normal maps are claimed or supplied.

`extraction-preview.png` is a labelled review contact sheet on a dark background. Its thumbnails are resized for viewing; the three sprite atlases are not. `extraction-receipt.json` records source hashes, source sizes, cut polygons, placement offsets and pixel counts. `verification.json` records checks against the saved PNGs, including exact RGB identity and layer recomposition. These assets are prepared for import and are not installed into the production ship definitions.

Rebuild from the repository root with `python scripts/prepare-bullfrog-sprites.py` (Pillow, NumPy and SciPy required). Originals remain unchanged in `docs/art/`.
