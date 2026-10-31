// Manually triggered production-art inspection; not a content tuning golden.
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.content.operations;
import phoenix.content.verification;
import phoenix.session.local;
import phoenix.presentation;
import phoenix.graphics.scene;
using namespace phoenix;

int main(int argc, char **argv)
{
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_knuckleduster_gallery base.zip");
		const auto catalog = resolve_packs({read_pack(read_file(argv[1]))});
		World world;
		world.load(catalog, "base.rescue-under-threat", {{"courier", "base.cruiser"}});
		const auto id = *world.ship_for_slot("courier");
		const auto scene = world.scene();
		const auto found = std::find_if(scene.begin(), scene.end(),
		                                [&](const auto &body)
		                                {
			                                return body.id == id;
		                                });
		if (found == scene.end())
			throw std::runtime_error("Cruiser missing from production scene");
		SceneBody ship = *found;
		ship.x = ship.y = ship.heading = ship.visual_height_mm = 0;
		const auto original = ship.attachments;
		if (original.size() != 5)
			throw std::runtime_error("Expected five independent main turret attachments");
		SetConfigFlags(FLAG_WINDOW_HIDDEN);
		InitWindow(1920, 1080, "Knuckleduster turret and material inspection");
		if (!IsWindowReady())
			throw std::runtime_error("Graphics context unavailable");
		{
			SceneRenderer renderer;
			renderer.preload(catalog, "base.cruiser");
			const char *names[] = {"bearings",     "normals",      "depth",  "traverse-90",
			                       "traverse-180", "traverse-270", "engines"};
			for (int pass = 0; pass < 7; ++pass)
			{
				ship.forward_thrust = pass == 6 ? 1000 : 0;
				ship.attachments = original;
				if (pass >= 3 && pass <= 5)
					for (auto &mount : ship.attachments)
						mount.heading = (mount.heading + (pass - 2) * 90000) % 360000;
				const auto diagnostic = pass == 1   ? SceneDiagnostic::normals
				                        : pass == 2 ? SceneDiagnostic::depth
				                                    : SceneDiagnostic::comic;
				BeginDrawing();
				ClearBackground({20, 24, 34, 255});
				for (int i = 0; i < 8; ++i)
				{
					const double angle = i * 3.14159265358979323846 / 4;
					Camera3D camera{{float(std::cos(angle) * 800), 0, float(-std::sin(angle) * 800)},
					                {0, 0, 0},
					                {0, 1, 0},
					                400,
					                CAMERA_ORTHOGRAPHIC};
					Rectangle region{float(i % 4 * 480), float(i / 4 * 540), 480, 540};
					renderer.draw(
					    catalog, std::vector<SceneBody>{ship}, camera, region,
					    {{"sun_direction", {-.5, .6, .6}}, {"ambient", .7}, {"ink_strength", .15}, {"shadows", true}},
					    0, diagnostic);
					for (const auto *name : {"turret_fore_dorsal", "turret_mid_dorsal", "turret_aft_dorsal",
					                         "turret_fore_ventral", "turret_aft_ventral", "engine_port_1",
					                         "engine_port_2", "engine_starboard_1", "engine_starboard_2"})
						if (!renderer.attachment(catalog, ship, name, camera))
							throw std::runtime_error(std::string("Missing socket: ") + name);
					DrawText((std::to_string(i * 45) + " degrees").c_str(), int(region.x) + 12, int(region.y) + 12, 20,
					         RAYWHITE);
				}
				auto image = LoadImageFromScreen();
				ExportImage(image, (std::string("build/knuckleduster/") + names[pass] + ".png").c_str());
				UnloadImage(image);
				EndDrawing();
			}
		}
		CloseWindow();
		std::cout << "Captured seven galleries, eight bearings, five independent turrets and four exhaust sockets.\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
