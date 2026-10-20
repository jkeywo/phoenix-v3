// Manually invoked graphical evidence, using asymmetric synthetic maps and
// source coordinates through the production pack loader and scene renderer.
void sprite_mirror_smoke()
{
	SceneRenderer renderer;
	const Camera3D camera{{0, 0, 10}, {0, 0, 0}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
	const Rectangle region{0, 0, 640, 480};
	const std::map<std::string, Bytes> assets{{"assets/albedo.png", png(RED, BLUE)},
	                                          {"assets/depth.png", png(BLACK, WHITE)},
	                                          {"assets/normal.png", png({218, 128, 218, 255}, {37, 128, 218, 255})},
	                                          {"assets/green.png", png(GREEN, GREEN)},
	                                          {"assets/white.png", png(WHITE, WHITE)}};
	auto source = material("albedo.png", "normal.png", false);
	source["depth"] = "comic-test:assets/depth.png";
	source["frames"][0]["pivot"] = {2, 3};
	source["frames"][0]["depth_scale"] = .25;
	source["frames"][0]["attachments"] = {{"engine_port", {{"x", 7}, {"y", 1}, {"layer", 1}, {"depth_offset", .1}}}};
	auto flipped = source["frames"];
	flipped[0]["mirror_x"] = true;
	source["entries"] = Json::array(
	    {{{"name", "source"}, {"frames", source.at("frames")}}, {{"name", "mirrored"}, {"frames", flipped}}});
	source.erase("frames");
	auto ship = entity("mirrored", source, 4);
	ship["render"]["engines"] = {{"sprite", material("green.png")}, {"scale", .12}, {"range", 1}, {"power", 1}};
	const auto catalog = load(Json::array({ship, entity("receiver", material("white.png", "", false), 12)}), assets);
	for (bool instanced : {false, true})
	{
		renderer.clear();
		std::vector<SceneBody> objects{body(300, "mirrored", 0, 0)};
		objects[0].field_instance = instanced;
		auto capture = [&](bool mirror, SceneDiagnostic diagnostic, const char *suffix, bool shadows = false)
		{
			objects[0].sprite_entry = mirror ? 1 : 0;
			BeginDrawing();
			ClearBackground(BLACK);
			renderer.draw(catalog, objects, camera, region, {{"sun_direction", {0, .6, .8}}, {"shadows", shadows}}, 0,
			              diagnostic);
			auto im = LoadImageFromScreen();
			const auto path = std::string("mirror-") + (instanced ? "instance-" : "ordinary-") +
			                  (mirror ? "flipped-" : "source-") + suffix + ".png";
			ExportImage(im, path.c_str());
			EndDrawing();
			return im;
		};
		for (auto diagnostic : {SceneDiagnostic::unlit, SceneDiagnostic::depth, SceneDiagnostic::normals})
		{
			const char *name = diagnostic == SceneDiagnostic::unlit   ? "albedo"
			                   : diagnostic == SceneDiagnostic::depth ? "depth"
			                                                          : "normals";
			auto a = capture(false, diagnostic, name), b = capture(true, diagnostic, name);
			const auto right = GetImageColor(a, 430, 240), left = GetImageColor(b, 209, 240);
			require(right.b > 100 && left.b > 100, "Mirrored asymmetric pivot lost the visible source region");
			if (diagnostic == SceneDiagnostic::unlit)
				require(right.b > right.r * 2 && left.b > left.r * 2 && GetImageColor(a, 209, 240).b == 0 &&
				            GetImageColor(b, 430, 240).b == 0,
				        "Albedo or source-pivot reflection failed");
			if (diagnostic == SceneDiagnostic::depth)
				require(std::abs(int(right.r) - int(left.r)) < 3 && right.r > 140,
				        "Mirrored depth sampled the wrong source half");
			if (diagnostic == SceneDiagnostic::normals)
				require(right.r < 80 && left.r > 180 && std::abs(int(right.b) - int(left.b)) < 3,
				        "Mirrored normal did not reverse its horizontal component");
			UnloadImage(a);
			UnloadImage(b);
		}
		if (!instanced)
		{
			objects[0].forward_thrust = 1000;
			auto a = capture(false, SceneDiagnostic::unlit, "engine"),
			     b = capture(true, SceneDiagnostic::unlit, "engine");
			int count[2] = {}, sums[2] = {}, rows[2] = {};
			const Image images[] = {a, b};
			for (int i = 0; i < 2; ++i)
				for (int y = 0; y < 480; ++y)
					for (int x = 0; x < 640; ++x)
					{
						const auto p = GetImageColor(images[i], x, y);
						if (p.g > 180 && p.g > p.r * 2 && p.g > p.b * 2)
						{
							++count[i];
							sums[i] += x;
							rows[i] += y;
						}
					}
			require(count[0] > 10 && count[1] > 10 && std::abs(sums[0] / count[0] + sums[1] / count[1] - 639) <= 2 &&
			            std::abs(rows[0] / count[0] - rows[1] / count[1]) <= 1 && sums[0] / count[0] > 440,
			        "Engine marker failed to reflect with its source depth and asymmetric pivot");
			UnloadImage(a);
			UnloadImage(b);
			objects[0].forward_thrust = 0;
		}
		objects.push_back(body(301, "receiver", 0, 2000));
		auto a = capture(false, SceneDiagnostic::shadows, "shadow", true),
		     b = capture(true, SceneDiagnostic::shadows, "shadow", true);
		int dark[2] = {}, differences = 0;
		const Image images[] = {a, b};
		for (int y = 100; y < 420; ++y)
			for (int x = 80; x < 560; ++x)
				for (int i = 0; i < 2; ++i)
				{
					const auto p = GetImageColor(images[i], x, y);
					dark[i] += p.r < 180;
					if (i == 0)
						differences += std::abs(int(p.r) - int(GetImageColor(b, 639 - x, y).r)) > 40;
				}
		require(dark[0] > 100 && std::abs(dark[0] - dark[1]) < 300 && differences < 300,
		        "Mirrored caster changed shadow coverage under horizontally symmetric sunlight");
		UnloadImage(a);
		UnloadImage(b);
	}
	std::cout << "PASS ordinary/instanced mirroring, asymmetric pivots, albedo, relief, normals, engines and shadows\n";
}
