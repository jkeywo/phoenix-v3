# GPU asset cache

Start with [assets.cppm](assets.cppm).

TextureCache keeps strong GPU handles for one content owner; identical decoded content shares handles until the last owner releases them on the render thread.

Dependencies: Content assets, raylib and portable byte/hash services.

Validation: Native/browser builds; authored asset verification and manually triggered graphics smoke.
