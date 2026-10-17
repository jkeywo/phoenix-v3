# Training content

`training/` is editable starter content, separate from synthetic test fixtures. Native builds package it through the production writer and verifier into `build/native/training.zip`. Import that ZIP in New game or Workshop. It provides a navigation mission, two allowed ships, one authored bridge console, a beacon and delegated traffic. Tuning and the simple generated placeholder sprites are for prototyping.

Eight-way sprite entries are ordered by camera bearing relative to ship heading: 0, 45, 90, 135, 180, 225, 270 and 315 degrees. Heading 0 points along world +X (east); 90 points along +Y (north). Camera-facing billboards use one image. Render scale is billboard height in metres, with source image aspect preserved.

To package other source folders, run `build/native/Release/phoenix_pack.exe source-directory output.zip [ordered-dependency.zip ...]`. The output is written only after production loading, dependency resolution and verification succeed. Workshop ZIP export remains the normal player authoring path.
