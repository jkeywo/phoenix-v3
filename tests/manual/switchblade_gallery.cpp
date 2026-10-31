// Explicit production-art smoke; never a CTest content/balance golden.
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
		if (argc < 3)
			throw std::runtime_error("Usage: phoenix_switchblade_gallery base.zip test-missions.zip");
		const auto catalog = resolve_packs({read_pack(read_file(argv[1])), read_pack(read_file(argv[2]))});
		World world;
		world.load(catalog, "base.asteroid-duel", {{"base.duellist", "base.courier"}});
		const auto scene = world.scene();
		const auto found = std::find_if(scene.begin(), scene.end(),
		                                [](const auto &b)
		                                {
			                                return b.definition == "base.pirate-raider";
		                                });
		if (found == scene.end())
			throw std::runtime_error("Production raider missing from duel scene");
		SceneBody ship = *found;
		ship.x = ship.y = ship.heading = ship.visual_height_mm = 0;
		SetConfigFlags(FLAG_WINDOW_HIDDEN);
		InitWindow(1920, 1080, "Switchblade material and turret inspection");
		if (!IsWindowReady())
			throw std::runtime_error("Graphics context unavailable");
		{
			SceneRenderer renderer;
			renderer.preload(catalog, ship.definition);
			for (int pass = 0; pass < 7; ++pass)
			{
				ship.forward_thrust = pass == 6 ? 1000 : 0;
				for (auto &mount : ship.attachments)
					mount.heading = pass == 3 ? 90000 : pass == 4 ? 180000 : pass == 5 ? 270000 : 0;
				const auto diagnostic = pass == 1   ? SceneDiagnostic::normals
				                        : pass == 2 ? SceneDiagnostic::depth
				                                    : SceneDiagnostic::comic;
				BeginDrawing();
				ClearBackground({20, 24, 34, 255});
				for (int i = 0; i < 8; ++i)
				{
					const double a = i * 3.14159265358979323846 / 4;
					Camera3D camera{{float(std::cos(a) * 40), 0, float(-std::sin(a) * 40)},
					                {0, 0, 0},
					                {0, 1, 0},
					                40,
					                CAMERA_PERSPECTIVE};
					Rectangle region{float(i % 4 * 480), float(i / 4 * 540), 480, 540};
					renderer.draw(
					    catalog, std::vector<SceneBody>{ship}, camera, region,
					    {{"sun_direction", {-.5, .6, .6}}, {"ambient", .65}, {"ink_strength", .2}, {"shadows", true}},
					    0, diagnostic);
					for (const auto *name : {"turret_forward", "turret_aft", "engine_upper", "engine_lower"})
						if (!renderer.attachment(catalog, ship, name, camera))
							throw std::runtime_error(std::string("Missing production socket: ") + name);
					DrawText((std::to_string(i * 45) + " degrees").c_str(), int(region.x) + 10, int(region.y) + 10, 20,
					         RAYWHITE);
				}
				const char *names[] = {"bearings",     "normals",      "depth",  "traverse-90",
				                       "traverse-180", "traverse-270", "engines"};
				auto image = LoadImageFromScreen();
				ExportImage(image, (std::string("build/switchblade/") + names[pass] + ".png").c_str());
				UnloadImage(image);
				EndDrawing();
			}
		}
		CloseWindow();
		std::cout << "Captured seven production renderer galleries, eight bearings, four registered sockets.\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
