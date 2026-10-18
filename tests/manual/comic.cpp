// Explicit graphical smoke, not an automatic golden or performance gate.
#include <raylib.h>
#include <rlgl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>
import phoenix.content;
import phoenix.presentation;
using namespace phoenix;
namespace
{
Bytes png(Color a, Color b)
{
	auto image = GenImageColor(8, 8, a);
	for (int y = 0; y < 8; y++)
		for (int x = 4; x < 8; x++)
			ImageDrawPixel(&image, x, y, b);
	int size = 0;
	auto *memory = ExportImageToMemory(image, ".png", &size);
	Bytes result(memory, memory + size);
	MemFree(memory);
	UnloadImage(image);
	return result;
}
void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}
Json material(const std::string &asset, const std::string &normal = "", bool emissive = true)
{
	Json j = {
	    {"albedo", "comic-test:assets/" + asset},
	    {"emissive", emissive},
	    {"frames",
	     Json::array(
	         {{{"name", "port"}, {"rect", {0, 0, 8, 8}}, {"pivot", {4, 4}}, {"azimuth", 90}, {"depth_scale", 0}}})}};
	if (!normal.empty())
		j["normal"] = "comic-test:assets/" + normal;
	return j;
}
Json entity(const char *id, const Json &sprite, float height)
{
	return {{"id", id},
	        {"render", {{"mode", "camera"}, {"scale", height}, {"sprite", sprite}}},
	        {"systems", Json::object()},
	        {"collision", {{"shape", "circle"}, {"radius_mm", 1}, {"mass_grams", 0}}}};
}
Catalog load(Json entities, const std::map<std::string, Bytes> &assets)
{
	PackDraft p;
	p.manifest = {{"schema", 1}, {"id", "comic-test"}};
	p.files = assets;
	p.files["definitions.json"] = json_bytes({{"schema", 3}, {"entities", entities}});
	return resolve_packs({read_pack(write_pack(p))});
}
SceneBody body(int id, const char *definition, int x, int y, int heading = 0, int thrust = 0)
{
	SceneBody b;
	b.id = id;
	b.definition = definition;
	b.x = x;
	b.y = y;
	b.vx = b.vy = 0;
	b.heading = heading;
	b.forward_thrust = thrust;
	return b;
}
} // namespace
int main()
{
	try
	{
		InitWindow(640, 480, "Comic renderer: synthetic depth and light smoke");
		SceneRenderer renderer;
		Camera3D camera{{0, 0, 10}, {0, 0, 0}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
		const Rectangle region{0, 0, 640, 480};
		std::map<std::string, Bytes> assets{{"assets/red.png", png(RED, RED)},
		                                    {"assets/blue.png", png(BLUE, BLUE)},
		                                    {"assets/white.png", png(WHITE, WHITE)},
		                                    {"assets/depth.png", png(BLACK, WHITE)},
		                                    {"assets/left.png", png({37, 128, 218, 255}, {37, 128, 218, 255})},
		                                    {"assets/right.png", png({218, 128, 218, 255}, {218, 128, 218, 255})}};
		auto red = material("red.png"), blue = material("blue.png");
		red["depth"] = "comic-test:assets/depth.png";
		red["frames"][0]["depth_scale"] = 1.5;
		auto catalog = load(Json::array({entity("red", red, 4), entity("blue", blue, 4)}), assets);
		std::vector<SceneBody> objects{body(1, "red", 0, -1000), body(2, "blue", 0, 0)};
		BeginDrawing();
		ClearBackground(BLACK);
		renderer.draw(catalog, objects, camera, region, {{"shadows", false}}, 0);
		auto capture = LoadImageFromScreen();
		ExportImage(capture, "comic-depth-smoke.png");
		EndDrawing();
		const auto left = GetImageColor(capture, 280, 240), right = GetImageColor(capture, 360, 240);
		UnloadImage(capture);
		renderer.clear();
		std::cout << "Depth samples: " << int(left.r) << ',' << int(left.b) << " / " << int(right.r) << ','
		          << int(right.b) << '\n';
		require(left.b > left.r * 2 && right.r > right.b * 2,
		        "pixel depth must place the far-centred blue surface ahead of red only on its recessed half");
		// A forward ship POV regularly leaves other sprite anchors behind the
		// eye. Their relief must stay behind it instead of folding into view.
		objects = {body(1, "red", 0, -20000), body(2, "blue", 0, 0)};
		BeginDrawing();
		ClearBackground(BLACK);
		renderer.draw(catalog, objects, camera, region, {{"shadows", false}}, 0);
		capture = LoadImageFromScreen();
		ExportImage(capture, "comic-forward-pov-smoke.png");
		EndDrawing();
		int rear_pixels = 0, front_pixels = 0;
		for (int y = 0; y < capture.height; ++y)
			for (int x = 0; x < capture.width; ++x)
			{
				const auto pixel = GetImageColor(capture, x, y);
				rear_pixels += pixel.r > 80 && pixel.r > pixel.b * 2;
				front_pixels += pixel.b > 80 && pixel.b > pixel.r * 2;
			}
		UnloadImage(capture);
		require(rear_pixels == 0 && front_pixels > 0,
		        "Sprite relief behind the eye must not project across the forward view");
		objects = {body(1, "red", 0, -1000), body(2, "blue", 0, 0)};
		renderer.clear();
		auto preview = LoadRenderTexture(320, 240);
		BeginDrawing();
		BeginTextureMode(preview);
		ClearBackground(BLACK);
		renderer.draw(catalog, objects, camera, {0, 0, 320, 240}, {{"shadows", false}}, 0, SceneDiagnostic::comic,
		              {320, 240});
		EndTextureMode();
		EndDrawing();
		capture = LoadImageFromTexture(preview.texture);
		const auto nested = GetImageColor(capture, 140, 120);
		UnloadImage(capture);
		UnloadRenderTexture(preview);
		require(nested.b > nested.r * 2, "Workshop preview must clip against its own framebuffer height");
		renderer.clear();
		red["frames"][0]["depth_scale"] = 0;
		catalog = load(Json::array({entity("red", red, 4), entity("blue", blue, 4)}), assets);
		BeginDrawing();
		ClearBackground(BLACK);
		renderer.draw(catalog, objects, camera, region, {{"shadows", false}}, 0);
		capture = LoadImageFromScreen();
		EndDrawing();
		const auto flat = GetImageColor(capture, 280, 240);
		UnloadImage(capture);
		require(flat.r > flat.b * 2, "zero relief must restore the ordinary nearer red billboard");
		renderer.clear();
		auto receiver = material("white.png", "", false);
		auto caster = receiver;
		caster["depth"] = "comic-test:assets/depth.png";
		caster["frames"][0]["depth_scale"] = .25;
		catalog = load(Json::array({entity("receiver", receiver, 5), entity("caster", caster, 1)}), assets);
		objects = {body(1, "receiver", 0, 0), body(2, "caster", 0, -2000)};
		BeginDrawing();
		ClearBackground(BLACK);
		renderer.draw(catalog, objects, camera, region, {{"sun_direction", {-.6, 0, .8}}, {"shadows", true}}, 0,
		              SceneDiagnostic::shadows);
		capture = LoadImageFromScreen();
		EndDrawing();
		const auto shadowed = GetImageColor(capture, 400, 240), lit = GetImageColor(capture, 240, 240);
		ExportImage(capture, "comic-shadow-smoke.png");
		UnloadImage(capture);
		require(lit.r > 200 && shadowed.r < lit.r - 100,
		        "depth-displaced alpha-covered sprite must cast a sun shadow onto another sprite");
		renderer.clear();
		auto hull = material("white.png", "", false);
		hull["frames"][0]["attachments"] = {{"engine_port", {{"x", 8}, {"y", 4}, {"layer", 1}}}};
		auto ship = entity("ship", hull, 2);
		ship["render"]["engines"] = {{"sprite", material("white.png")}, {"scale", .02}, {"range", 10}, {"power", 2}};
		catalog = load(Json::array({ship, entity("behind", material("white.png", "left.png", false), 1),
		                            entity("ahead", material("white.png", "right.png", false), 1)}),
		               assets);
		objects = {body(1, "ship", 0, 0, 180000, 1000), body(2, "behind", 3000, 0), body(3, "ahead", -3000, 0)};
		const Json light = {{"sun_intensity", 0}, {"ambient", .1}, {"shadows", false}};
		BeginDrawing();
		ClearBackground(BLACK);
		renderer.draw(catalog, objects, camera, region, light, 0, SceneDiagnostic::smooth);
		capture = LoadImageFromScreen();
		ExportImage(capture, "comic-engine-smoke.png");
		EndDrawing();
		const auto aft = GetImageColor(capture, 475, 240), fore = GetImageColor(capture, 165, 240);
		UnloadImage(capture);
		require(aft.r > fore.r + 50 && aft.r > aft.b + 50,
		        "engine cone must illuminate aft-facing receiver without lighting an equally close receiver in front");

		ExplosionEffect event;
		event.position = {-3, 0, 1};
		event.started = 0;
		renderer.explosion(event);
		BeginDrawing();
		ClearBackground(BLACK);
		renderer.draw(catalog, objects, camera, region, light, .2, SceneDiagnostic::smooth);
		capture = LoadImageFromScreen();
		ExportImage(capture, "comic-effects-smoke.png");
		EndDrawing();
		const auto burst = GetImageColor(capture, 165, 240);
		UnloadImage(capture);
		require(burst.r > fore.r + 20,
		        "internal explosion event must illuminate a receiver without a gameplay trigger");
		BeginDrawing();
		ClearBackground({8, 16, 23, 255});
		DrawText("Shared radar symbols / native + browser", 30, 24, 22, WHITE);
		const RadarIcon icons[] = {RadarIcon::ship, RadarIcon::asteroid, RadarIcon::sun, RadarIcon::contact};
		for (int column = 0; column < 4; ++column)
		{
			const float x = 160.f + column * 125;
			const std::string name(radar_icon_name(icons[column]));
			DrawText(name.c_str(), static_cast<int>(x - 30), 72, 18, SKYBLUE);
			for (int row = 0; row < 4; ++row)
			{
				const float diameter = 12.f + row * 4;
				const float y = 130.f + row * 70;
				if (column == 0)
					DrawText(TextFormat("%d px", static_cast<int>(diameter)), 30, static_cast<int>(y - 8), 18, WHITE);
				draw_radar_icon(icons[column], {x, y}, diameter / 2, 90000, SKYBLUE);
			}
		}
		DrawText("Ship heading follows the contact; other symbols stay upright.", 30, 430, 18, WHITE);
		rlDrawRenderBatchActive();
		capture = LoadImageFromScreen();
		ExportImage(capture, "radar-icons-smoke.png");
		UnloadImage(capture);
		EndDrawing();

		// Manually triggered navigation-aid evidence through verified synthetic content.
		renderer.clear();
		auto navigation_ship = entity("navigation", material("white.png"), 1);
		navigation_ship["render"]["reference_grid"] = {
		    {"minor_spacing", 6}, {"major_spacing", 30}, {"patch_radius", 90}, {"fade_band", 60}, {"plane_y", -1.4}};
		catalog = load(Json::array({navigation_ship}), assets);
		objects = {body(1, "navigation", 0, 0)};
		SceneEnvironment navigation;
		navigation.own = objects[0];
		navigation.grid =
		    compile_reference_grid(catalog.entities.at("navigation").at("render").at("reference_grid"), "grid");
		navigation.dust =
		    compile_dust({{"count", 211}, {"width", .2}, {"half_width", 20}, {"depth", 60}}, catalog, false, "dust");
		Camera3D navigation_camera{{0, 8, 20}, {0, 0, 0}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
		auto navigation_capture = [&](const SceneEnvironment &env, Camera3D eye, Rectangle rect, const char *name)
		{
			BeginDrawing();
			ClearBackground(BLACK);
			renderer.draw(catalog, objects, eye, rect, {{"shadows", false}}, 0, SceneDiagnostic::unlit, {}, env);
			rlDrawRenderBatchActive();
			auto result = LoadImageFromScreen();
			if (name)
				ExportImage(result, name);
			EndDrawing();
			return result;
		};
		auto differences = [](Image a, Image b)
		{
			int pixels = 0;
			for (int y = 0; y < a.height; ++y)
				for (int x = 0; x < a.width; ++x)
				{
					const auto ca = GetImageColor(a, x, y), cb = GetImageColor(b, x, y);
					if (std::abs(int(ca.r) - cb.r) + std::abs(int(ca.g) - cb.g) + std::abs(int(ca.b) - cb.b) > 6)
						++pixels;
				}
			return pixels;
		};
		auto disabled = navigation;
		disabled.dust_enabled = disabled.grid_enabled = false;
		auto empty = navigation_capture(disabled, navigation_camera, region, nullptr);
		auto grid_only = navigation;
		grid_only.dust_enabled = false;
		auto grid_image = navigation_capture(grid_only, navigation_camera, region, "navigation-grid-smoke.png");
		require(differences(empty, grid_image) > 200,
		        "The authored world grid must visibly render through the production scene path");
		auto dust_only = navigation;
		dust_only.grid_enabled = false;
		auto stationary = navigation_capture(dust_only, navigation_camera, region, "navigation-stationary-smoke.png");
		require(differences(empty, stationary) > 10, "The final V2 fixed-density field must draw motes at rest");
		for (const auto velocity : {Vector2{15000, 0}, Vector2{-15000, 0}, Vector2{0, 15000}, Vector2{0, 75000}})
		{
			dust_only.own->vx = static_cast<std::int64_t>(velocity.x);
			dust_only.own->vy = static_cast<std::int64_t>(velocity.y);
			auto moving =
			    navigation_capture(dust_only, navigation_camera, region,
			                       TextFormat("navigation-motion-%d-%d-smoke.png", int(velocity.x), int(velocity.y)));
			require(differences(stationary, moving) > 10, "True velocity must change the projected dust streaks");
			UnloadImage(moving);
		}
		UnloadImage(stationary);
		navigation.reduced_motion = true;
		auto reduced = navigation_capture(navigation, navigation_camera, region, nullptr);
		require(differences(empty, reduced) == 0,
		        "Reduced motion suppresses both aids without changing the ship scene");
		UnloadImage(reduced);
		UnloadImage(empty);
		navigation.reduced_motion = false;
		auto offset =
		    navigation_capture(navigation, navigation_camera, {120, 60, 400, 300}, "navigation-offset-smoke.png");
		require(GetImageColor(offset, 20, 20).r == 0 && GetImageColor(offset, 600, 400).b == 0,
		        "Navigation aids must stay inside their display region");
		UnloadImage(offset);
		// A large world coordinate must not change the repeating lattice phase.
		grid_only.own->x += 600000000;
		objects[0].x += 600000000;
		navigation_camera.position.x += 600000;
		navigation_camera.target.x += 600000;
		auto distant = navigation_capture(grid_only, navigation_camera, region, "navigation-distant-smoke.png");
		require(differences(grid_image, distant) < 20,
		        "Rebasing by a whole number of grid cells must preserve the lattice phase");
		UnloadImage(grid_image);
		grid_only.own->x += 3000;
		objects[0].x += 3000;
		navigation_camera.position.x += 3;
		navigation_camera.target.x += 3;
		auto half_cell = navigation_capture(grid_only, navigation_camera, region, "navigation-half-cell-smoke.png");
		require(differences(distant, half_cell) > 200,
		        "Moving ship and camera together must reveal a different world-locked lattice phase");
		UnloadImage(half_cell);
		UnloadImage(distant);
		auto underside_camera = navigation_camera;
		underside_camera.position.y = -8;
		auto underside = navigation_capture(grid_only, underside_camera, region, "navigation-underside-smoke.png");
		UnloadImage(underside);
		auto target = LoadRenderTexture(320, 240);
		BeginDrawing();
		BeginTextureMode(target);
		ClearBackground(BLACK);
		renderer.draw(catalog, objects, navigation_camera, {25, 20, 270, 200}, {{"shadows", false}}, 0,
		              SceneDiagnostic::unlit, {320, 240}, grid_only);
		EndTextureMode();
		EndDrawing();
		auto nested_navigation = LoadImageFromTexture(target.texture);
		ExportImage(nested_navigation, "navigation-nested-smoke.png");
		UnloadImage(nested_navigation);
		UnloadRenderTexture(target);
		std::cout << "PASS navigation grid, stationary/moving dust, reduced motion and clipped/nested regions\n";

		std::cout << "PASS production-loader graphical sprite depth, flat fallback, directional engines and internal "
		             "explosion lighting\n";
		renderer.clear();
#ifndef __EMSCRIPTEN__
		CloseWindow();
#endif
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "FAIL " << e.what() << '\n';
		return 1;
	}
}
