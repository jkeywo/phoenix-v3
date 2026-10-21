# Cruiser source maps

`hull-depth-draft.png` is the built-in image-generated draft registered to the original hull atlas. `turret-depth-draft.png` is an authored smooth geometric approximation; the image service rejected both attempts to generate that map. Prompts and provenance are in `provenance.json`.

Regenerate the packaged hull/turret materials explicitly from the original ignored `raw/cruiser` drawings:

```powershell
python scripts/prepare-cruiser-assets.py --hull-depth art/cruiser/hull-depth-draft.png --turret-depth art/cruiser/turret-depth-draft.png
python scripts/preview-cruiser-markers.py
```

The preparation code calibrates each unique view independently and derives its normals from that same depth field. Mirrored bearings reuse the original rectangles and unreflected source coordinates. Normal builds use only tracked assets under `content/base/assets/cruiser`; they do not require image generation, Pillow or raw inputs.

The separate marker preview reads tracked materials and writes annotated bearings to `build/cruiser-markers/`. Filled points are visible registrations; hollow points retain inferred hidden positions. Labels distinguish physical port/starboard identities after mirroring.

Production albedo quantizes colour to fit the bounded base archive while preserving every original alpha value. Original colour sheets remain unchanged. Marker positions are authored approximations, with separate physical firing offsets in the ship fit. Bay points are visual registration references; the present bay runtime launches above the carrier at its ordinary forward clearance point.

These five-view illustrations cannot establish exact geometry between bearings. Depth and normals support visual relief/occlusion only and never change collision volumes.
