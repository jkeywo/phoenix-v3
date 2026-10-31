// Explicit production-art inspection; not a CTest content/balance golden.
#include <raylib.h>
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
		if (argc != 2)
			throw std::runtime_error("Usage: phoenix_celestial_gallery base.zip");
		const auto catalog = resolve_packs({read_pack(read_file(argv[1]))});
		World world;
		world.load(catalog, "base.rescue-under-threat", {{"courier", "base.courier"}});
		std::vector<std::string> definitions;
		for (const auto *rarity : {"common", "uncommon", "rare"})
			for (int i = 1; i <= 4; ++i)
				definitions.push_back(std::string("base.asteroid-") + rarity + "-0" + std::to_string(i));
		definitions.push_back("base.delivery-planet");
		definitions.push_back("base.delivery-star");
		SetConfigFlags(FLAG_WINDOW_HIDDEN);
		InitWindow(1600, 1000, "Phoenix celestial material inspection");
		if (!IsWindowReady())
			throw std::runtime_error("Graphics context unavailable");
		{
			SceneRenderer renderer;
			for (const auto &definition : definitions)
				renderer.preload(catalog, definition);
			for (int pass = 0; pass < 4; ++pass)
			{
				const auto diagnostic = pass == 1   ? SceneDiagnostic::depth
				                        : pass == 2 ? SceneDiagnostic::normals
				                                    : SceneDiagnostic::comic;
				BeginDrawing();
				ClearBackground({25, 29, 39, 255});
				for (std::size_t i = 0; i < definitions.size(); ++i)
				{
					SceneBody body;
					body.id = i + 1;
					body.definition = definitions[i];
					// Individual framing only; production scales remain unchanged.
					body.visual_scale =
					    12.f / catalog.entities.at(body.definition).at("render").at("scale").get<float>();
					Camera3D camera{{40, 0, 0}, {0, 0, 0}, {0, 1, 0}, 28, CAMERA_PERSPECTIVE};
					Rectangle region{float(i % 4 * 400), float(i / 4 * 250), 400, 250};
					renderer.draw(catalog, std::vector<SceneBody>{body}, camera, region,
					              {{"sun_direction", {pass == 3 ? .6 : -.6, .5, .6}},
					               {"ambient", .6},
					               {"ink_strength", .2},
					               {"shadows", true}},
					              0, diagnostic);
					DrawText(definitions[i].c_str(), int(region.x) + 8, int(region.y) + 8, 17, RAYWHITE);
				}
				const char *names[] = {"comic", "depth", "normals", "opposite-light"};
				auto image = LoadImageFromScreen();
				ExportImage(image, (std::string("build/celestial/") + names[pass] + ".png").c_str());
				UnloadImage(image);
				EndDrawing();
			}
		}
		CloseWindow();
		std::cout << "Loaded production Rescue; captured all 14 celestial materials in four renderer passes.\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
