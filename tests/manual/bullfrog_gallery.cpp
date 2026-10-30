// Manually invoked production-art inspection, never a CTest balance assertion.
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
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
			throw std::runtime_error("Usage: phoenix_bullfrog_gallery base.zip");
		const auto catalog = resolve_packs({read_pack(read_file(argv[1]))});
		World world;
		world.load(catalog, "base.rescue-under-threat", {{"courier", "base.courier"}});
		const auto id = *world.ship_for_slot("courier");
		auto scene = world.scene();
		const auto found = std::find_if(scene.begin(), scene.end(),
		                                [&](const auto &b)
		                                {
			                                return b.id == id;
		                                });
		if (found == scene.end())
			throw std::runtime_error("Courier missing from production scene");
		SceneBody ship = *found;
		ship.x = ship.y = ship.heading = ship.visual_height_mm = 0;
		SetConfigFlags(FLAG_WINDOW_HIDDEN);
		InitWindow(1920, 1080, "Bullfrog production material gallery");
		if (!IsWindowReady())
			throw std::runtime_error("Graphics context unavailable");
		{
			SceneRenderer renderer;
			renderer.preload(catalog, "base.courier");
			for (int pass = 0; pass < 5; ++pass)
			{
				ship.forward_thrust = pass == 4 ? 1000 : 0;
				for (auto &mount : ship.attachments)
					mount.heading = pass == 3 ? 90000 : 0;
				const auto diagnostic = pass == 1   ? SceneDiagnostic::normals
				                        : pass == 2 ? SceneDiagnostic::depth
				                                    : SceneDiagnostic::comic;
				BeginDrawing();
				ClearBackground({20, 24, 34, 255});
				for (int i = 0; i < 8; ++i)
				{
					const double angle = i * 3.14159265358979323846 / 4;
					Camera3D camera{{float(std::cos(angle) * 65), 0, float(-std::sin(angle) * 65)},
					                {0, 0, 0},
					                {0, 1, 0},
					                35,
					                CAMERA_PERSPECTIVE};
					Rectangle region{float(i % 4 * 480), float(i / 4 * 540), 480, 540};
					renderer.draw(
					    catalog, std::vector<SceneBody>{ship}, camera, region,
					    {{"sun_direction", {-.5, .6, .6}}, {"ambient", .6}, {"ink_strength", .25}, {"shadows", true}},
					    0, diagnostic);
					DrawText((std::to_string(i * 45) + " degrees").c_str(), int(region.x) + 10, int(region.y) + 10, 20,
					         RAYWHITE);
				}
				auto image = LoadImageFromScreen();
				const char *name = pass == 0   ? "bearings"
				                   : pass == 1 ? "normals"
				                   : pass == 2 ? "depth"
				                   : pass == 3 ? "traverse"
				                               : "engines";
				ExportImage(image, (std::string("build/bullfrog-") + name + ".png").c_str());
				UnloadImage(image);
				EndDrawing();
			}
		}
		CloseWindow();
		std::cout << "Captured five production renderer galleries across eight bearings.\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
