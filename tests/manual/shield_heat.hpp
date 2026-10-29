// Explicit GPU smoke only; synthetic assets travel through the real pack loader.
void shield_heat_smoke()
{
	for (const bool models : {false, true})
	{
		PackDraft pack;
		pack.manifest = {{"schema", 1}, {"id", "comic-test"}};
		Json hull;
		if (models)
		{
			pack.manifest["base"] = {{"render_mode", "models"}};
			pack.files["assets/hull.glb"] = phoenix::test::model_fixture(
			    {{"nodes", Json::array({{{"mesh", 0}, {"translation", {-.5, -.5, 0}}},
			                            {{"mesh", 0}, {"rotation", {0, 0, 1, 0}}, {"translation", {.5, .5, 0}}}})},
			     {"scenes", Json::array({{{"nodes", {0, 1}}}})},
			     {"materials", Json::array({{{"pbrMetallicRoughness", {{"baseColorFactor", {.02, .02, .02, 1}}}}}})},
			     {"meshes", Json::array({{{"primitives",
			                               Json::array({{{"attributes", {{"POSITION", 0}}}, {"material", 0}}})}}})}});
			hull = {{"id", "hull"},
			        {"render",
			         {{"scale", 1}, {"model", {{"asset", "comic-test:assets/hull.glb"}, {"rotation", {0, 90, 0}}}}}},
			        {"systems", Json::object()},
			        {"collision", {{"shape", "circle"}, {"radius_mm", 2500}, {"mass_grams", 0}}}};
		}
		else
		{
			pack.files["assets/hull.png"] = png({5, 5, 5, 255}, {5, 5, 5, 255});
			hull = entity("hull", material("hull.png"), 1);
		}
		auto blocker = hull;
		blocker["id"] = "blocker";
		blocker["render"]["scale"] = 12;
		pack.files["definitions.json"] = json_bytes({{"schema", 3}, {"entities", Json::array({hull, blocker})}});
		const auto catalog = resolve_packs({read_pack(write_pack(pack))});
		SceneRenderer renderer;
		std::vector<SceneBody> objects{body(1, "hull", 0, 0)};
		objects[0].shield_radius = 2.5f;
		SceneShieldImpact hit;
		hit.event = 1;
		hit.absorbed = 30;
		hit.direction = {0, 0, 1};
		objects[0].shield_impacts = {hit};
		Camera3D camera{{0, 0, 10}, {}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
		auto target = LoadRenderTexture(320, 240);
		auto render = [&](double time, const char *name)
		{
			objects[0].shield_time = time;
			BeginDrawing();
			BeginTextureMode(target);
			ClearBackground(BLACK);
			DrawRectangle(0, 0, 320, 8, GREEN);
			DrawRectangle(0, 232, 320, 8, GREEN);
			DrawRectangle(0, 8, 8, 224, GREEN);
			DrawRectangle(312, 8, 8, 224, GREEN);
			renderer.draw(catalog, objects, camera, {8, 8, 304, 224}, {{"shadows", false}}, time,
			              SceneDiagnostic::unlit, {320, 240});
			EndTextureMode();
			EndDrawing();
			auto capture = LoadImageFromTexture(target.texture);
			ImageFlipVertical(&capture);
			if (name)
				ExportImage(capture,
				            (std::string("shield-") + (models ? "model-" : "sprite-") + name + ".png").c_str());
			// The shield pass must preserve the preview's clipped border.
			const auto corner = GetImageColor(capture, 2, 2);
			require(corner.g == GREEN.g && corner.r == GREEN.r, "Shield escaped offscreen preview clipping");
			return capture;
		};
		auto coloured = [](Image image, bool hot)
		{
			int count = 0;
			for (int y = 8; y < 232; ++y)
				for (int x = 8; x < 312; ++x)
				{
					const auto c = GetImageColor(image, x, y);
					if (c.r > 120 && (hot ? c.g > 120 : c.g < 100))
						++count;
				}
			return count;
		};
		auto initial = render(0, "hot");
		require(coloured(initial, true) > 10, "Shield failed to draw white/yellow hex edges");
		UnloadImage(initial);
		const auto before = renderer.instance_statistics().at("shields");
		auto duplicate = render(0, nullptr);
		UnloadImage(duplicate);
		require(renderer.instance_statistics().at("shields") == before,
		        "Repeated view advanced shield heat or replayed its impact");
		camera.position = {2, 0, 10};
		auto second_view = render(0, nullptr);
		UnloadImage(second_view);
		require(renderer.instance_statistics().at("shields") == before,
		        "Second camera advanced the shared shield field");
		camera.position = {0, 0, 10};
		auto cool = render(1.5, "cool");
		require(coloured(cool, false) > 10 && coloured(cool, true) == 0, "Shield did not cool through red/orange");
		UnloadImage(cool);
		// Add a nearby impact while the earlier patch is still cooling.
		hit.event = 2;
		hit.tick = 30;
		hit.time = 1.5;
		hit.direction = {.12f, 0, .992774f};
		objects[0].shield_impacts.push_back(hit);
		auto overlap = render(1.5, "overlap");
		require(coloured(overlap, true) > 10, "Overlapping hit failed to reheat the shared field");
		UnloadImage(overlap);
		// Move/turn the ship and camera together: the patch stays attached.
		objects[0].x = 20000;
		objects[0].heading = 90000;
		camera.position = {30, 0, 0};
		camera.target = {20, 0, 0};
		auto turned = render(1.5, "turned");
		require(coloured(turned, true) > 10, "Shield hotspot did not follow ship translation/heading");
		UnloadImage(turned);
		auto expired = render(3.6, "expired");
		require(coloured(expired, true) == 0 && coloured(expired, false) == 0 &&
		            renderer.instance_statistics().at("shields").at("active") == 0,
		        "Shield heat failed to expire/release textures");
		UnloadImage(expired);
		// Rewind, and hit at the longitude seam above the equator.
		objects[0].x = 0;
		objects[0].heading = 0;
		hit.event = 1;
		hit.tick = 0;
		hit.time = 0;
		hit.direction = {-.8f, .6f, 0};
		objects[0].shield_impacts = {hit};
		camera.position = {-8, 6, 0};
		camera.target = {};
		auto seam = render(0, "seam");
		require(coloured(seam, true) > 10, "Seam-crossing/elevated shield impact was lost");
		UnloadImage(seam);
		renderer.clear();
		camera.position = {0, 6, 10};
		camera.target = {0, 6, 0};
		objects[0].visual_height_mm = 6000;
		hit.direction = {0, 0, 1};
		objects[0].shield_impacts = {hit};
		objects.push_back(body(2, "blocker", 0, -5000));
		objects[1].visual_height_mm = 6000;
		auto occluded = render(0, "occluded");
		require(coloured(occluded, true) == 0 && coloured(occluded, false) == 0,
		        "Shield heat ignored opaque foreground depth");
		UnloadImage(occluded);
		// Reconstruct retained overlapping impacts after skipping presentation frames.
		objects.resize(1);
		renderer.clear();
		hit.absorbed = 15;
		objects[0].shield_impacts = {hit};
		auto first = render(0, nullptr);
		UnloadImage(first);
		hit.event = 2;
		hit.time = .3;
		hit.tick = 6;
		objects[0].shield_impacts.push_back(hit);
		auto second = render(.3, nullptr);
		UnloadImage(second);
		auto continuous = render(.9, nullptr);
		renderer.clear();
		auto reconstructed = render(.9, "retained");
		int mismatch = 0;
		for (int y = 8; y < 232; ++y)
			for (int x = 8; x < 312; ++x)
			{
				const auto a = GetImageColor(continuous, x, y), b = GetImageColor(reconstructed, x, y);
				if (std::abs(a.r - b.r) > 3 || std::abs(a.g - b.g) > 3 || std::abs(a.b - b.b) > 3)
					++mismatch;
			}
		require(mismatch == 0, "Skipped snapshots reconstructed different overlapping shield heat");
		UnloadImage(continuous);
		UnloadImage(reconstructed);
		UnloadRenderTexture(target);
		renderer.clear();
		std::cout << "PASS " << (models ? "model" : "sprite")
		          << " shield heat, overlap, cooling, pause/dedup, attachment, seam and offscreen clipping\n";
	}
}
