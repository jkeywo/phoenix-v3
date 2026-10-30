#include <raylib.h>
#include <imgui.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.tools;
import phoenix.presentation;
import phoenix.workshop.map;

using namespace phoenix;
int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: phoenix_map_authoring_probe <disposable-three-entity-pack>\n";
		return 2;
	}
	try
	{
		WorkshopDocument document;
		document.load_pack(read_pack_path(argv[1]).archive());
		if (document.map().at("entities").size() != 3)
			throw std::runtime_error("Use the independent three-entity authoring fixture");
		open_window();
		begin_tools();
		SetWindowSize(1200, 800);
		SetWindowTitle("Phoenix map authoring capture");
		SetTargetFPS(60);
		{
			MapEditor editor;
			editor.touch_editing(true);
			MapEditorSelection selection;
			ToolMessage status;
			const auto initial = document.map().at("entities");
			Json moved;
			float x = 0, y = 0;
			for (int frame = 0; frame < 38 && !WindowShouldClose(); ++frame)
			{
				const auto canvas = editor.canvas_bounds();
				ToolFrame input;
				input.area = {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
				input.keyboard = true;
				std::vector<ToolPointerSample> pointers;
				auto point = [&](ToolPointerSample::Phase phase, float px, float py)
				{
					x = px;
					y = py;
					pointers.push_back({phase, 1, px, py});
				};
				if (frame == 5)
					point(ToolPointerSample::down, canvas[0] + 12, canvas[1] + 12);
				if (frame == 8)
					point(ToolPointerSample::move, canvas[0] + canvas[2] - 12, canvas[1] + canvas[3] - 12);
				if (frame == 11)
					point(ToolPointerSample::up, x, y);
				if (frame == 14)
				{
					if (selection.instances.size() != 3)
						throw std::runtime_error("Rectangle must select all three projected instances");
					const auto &anchor = initial.front();
					point(ToolPointerSample::down, canvas[0] + canvas[2] / 2 + anchor.at("x_mm").get<float>() * .004f,
					      canvas[1] + canvas[3] / 2 - anchor.at("y_mm").get<float>() * .004f);
				}
				if (frame == 17)
					point(ToolPointerSample::move, x + 80, y - 40);
				if (frame == 20)
					point(ToolPointerSample::up, x, y);
				if (frame == 23)
				{
					moved = document.map().at("entities");
					for (std::size_t i = 0; i < initial.size(); ++i)
						if (moved[i].at("x_mm").get<std::int64_t>() !=
						        initial[i].at("x_mm").get<std::int64_t>() + 20000 ||
						    moved[i].at("y_mm").get<std::int64_t>() !=
						        initial[i].at("y_mm").get<std::int64_t>() + 10000)
							throw std::runtime_error("Group drag must preserve all relative offsets");
					point(ToolPointerSample::down, x, y);
				}
				if (frame == 26)
					point(ToolPointerSample::move, x - 40, y + 80);
				if (frame == 29)
					point(ToolPointerSample::cancel, x, y);
				if (frame == 32 && document.map().at("entities") != moved)
					throw std::runtime_error("Cancelled drag must restore every starting position");
				input.pointers = pointers;
				BeginDrawing();
				ClearBackground(BLACK);
				draw_tool_frame(input,
				                [&]
				                {
					                ImGui::SetNextWindowPos({0, 0});
					                ImGui::SetNextWindowSize({input.area.width, input.area.height});
					                editor.draw(document, selection, status, false, input,
					                            []
					                            {
					                            });
				                });
				EndDrawing();
				if (frame == 34)
					TakeScreenshot("build/workshop-map-selection.png");
			}
		}
		end_tools();
		close_window();
		std::cout << "PASS production map rectangle selection, group movement and cancellation\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
