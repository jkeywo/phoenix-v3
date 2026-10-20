// Explicit graphical smoke: current-frame invariants, not production-content goldens.
void skybox_smoke(const Catalog &catalog, const std::string &reference)
{
	SceneRenderer renderer;
	SceneEnvironment environment;
	environment.skybox = reference;
	environment.dust_enabled = environment.grid_enabled = false;
	renderer.preload_environment(catalog, environment);
	const Rectangle region{64, 48, 640, 480};
	Camera3D camera{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 65, CAMERA_PERSPECTIVE};
	Json lighting = Json::object();
	const auto render = [&](const std::string &name)
	{
		BeginDrawing();
		ClearBackground(MAGENTA);
		renderer.draw(catalog, {}, camera, region, lighting, 0, SceneDiagnostic::comic, {}, environment);
		auto result = LoadImageFromScreen();
		EndDrawing();
		if (!name.empty())
			capture(name);
		return result;
	};
	auto initial = render("skybox-origin.png");
	auto outside = GetImageColor(initial, 8, 8);
	require(outside.r == 255 && outside.g == 0 && outside.b == 255, "skybox respects viewport boundary");
	unsigned drawn = 0;
	for (int y = 50; y < 525; y += 8)
		for (int x = 66; x < 700; x += 8)
		{
			const auto p = GetImageColor(initial, x, y);
			drawn += p.r != 255 || p.g != 0 || p.b != 255;
		}
	require(drawn > 4000, "skybox covers the viewport");
	camera.position = {1234, 50, -2345};
	camera.target = {1235, 50, -2345};
	auto translated = render("skybox-translated.png");
	auto *a = LoadImageColors(initial), *b = LoadImageColors(translated);
	require(std::equal(a, a + initial.width * initial.height, b,
	                   [](Color x, Color y)
	                   {
		                   return x.r == y.r && x.g == y.g && x.b == y.b;
	                   }),
	        "infinite skybox has no translation parallax");
	UnloadImageColors(a);
	UnloadImageColors(b);
	UnloadImage(initial);
	UnloadImage(translated);
	camera.position = {};
	for (int direction = 0; direction < 8; ++direction)
	{
		const float angle = direction * PI / 4;
		camera.target = {std::cos(angle), 0, std::sin(angle)};
		UnloadImage(render("skybox-heading-" + std::to_string(direction) + ".png"));
	}
	for (const int pole : {-1, 1})
	{
		camera.target = {0.001f, static_cast<float>(pole), 0};
		UnloadImage(render(pole < 0 ? "skybox-south.png" : "skybox-north.png"));
	}
	camera.target = {1, 0, 0};
	lighting = {{"environment",
	             {{"nebulae", Json::array({{{"shape", 0},
	                                        {"x", 0},
	                                        {"y", 0},
	                                        {"radius", 100},
	                                        {"inner_radius", 0},
	                                        {"half_width", 100},
	                                        {"half_height", 100},
	                                        {"height", 100},
	                                        {"density", .02},
	                                        {"colour", {0.3, 0.1, 0.4}}}})}}}};
	UnloadImage(render("skybox-fog.png"));
	lighting = Json::object();
	environment.skybox.clear();
	auto blank = render("skybox-none.png");
	const auto empty = GetImageColor(blank, 320, 240);
	require(empty.r == 255 && empty.g == 0 && empty.b == 255, "None preserves the caller's blank background");
	UnloadImage(blank);
	renderer.clear();
	std::cout << "PASS skybox viewport, translation, eight headings, poles and None\n";
}
