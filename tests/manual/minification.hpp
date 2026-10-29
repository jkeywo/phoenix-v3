// Explicit GPU smoke: subpixel source detail should average instead of shimmer.
void sprite_minification_smoke()
{
	auto image = GenImageColor(512, 256, RED);
	for (int y = 0; y < 256; ++y)
		for (int x = 1; x < 257; ++x)
			ImageDrawPixel(&image, x, y, (x + y) % 2 ? WHITE : BLACK);
	int size = 0;
	auto *memory = ExportImageToMemory(image, ".png", &size);
	const Bytes bytes(memory, memory + size);
	MemFree(memory);
	UnloadImage(image);
	auto sprite = material("detail.png");
	sprite["frames"][0]["rect"] = {1, 0, 256, 256};
	sprite["frames"][0]["pivot"] = {128, 128};
	const auto catalog = load(Json::array({entity("detail", sprite, 1)}), {{"assets/detail.png", bytes}});
	for (bool instanced : {false, true})
	{
		SceneRenderer renderer;
		std::vector<SceneBody> objects{body(700, "detail", 0, 0)};
		objects[0].field_instance = instanced;
		for (float pixels : {7.f, 17.f, 39.f})
			for (float phase : {0.f, .37f})
			{
				const float offset = phase / pixels;
				const Camera3D camera{{offset, 0, 10}, {offset, 0, 0}, {0, 1, 0}, 480 / pixels, CAMERA_ORTHOGRAPHIC};
				BeginDrawing();
				ClearBackground(BLACK);
				renderer.draw(catalog, objects, camera, {0, 0, 640, 480}, {{"shadows", false}}, 0,
				              SceneDiagnostic::unlit);
				auto capture = LoadImageFromScreen();
				EndDrawing();
				for (int y = 240 - int(pixels / 2) + 1; y < 240 + int(pixels / 2) - 1; ++y)
					for (int x = 320 - int(pixels / 2) + 1; x < 320 + int(pixels / 2) - 1; ++x)
					{
						const auto p = GetImageColor(capture, x, y);
						require(p.r > 110 && p.r < 145 && std::abs(int(p.r) - int(p.g)) < 3,
						        "Minified detail aliased or sampled a neighbouring atlas frame");
					}
				if (pixels == 39 && phase == 0)
					ExportImage(capture,
					            instanced ? "minification-instance-smoke.png" : "minification-ordinary-smoke.png");
				UnloadImage(capture);
			}
	}
	std::cout << "PASS minified detail averaging and unaligned atlas isolation, ordinary and instanced\n";
}
