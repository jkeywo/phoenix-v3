# Asset compilation

Read types.cppm (phoenix.content.assets) and image_codec.cppm first.

Verified sprite/model/audio declarations and catalogue-independent PNG/WebP conversion.

Dependencies: Catalogue and portable data; private pinned decoders. No GPU or window APIs.

Ownership: SpriteCompiler caches dimensions for one unchanged catalogue. Codec caches are bounded optional acceleration only.

Validation: Content codecs, sprite materials, audio contracts and asset verification cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
