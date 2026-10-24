#include <raylib.h>
#include <iostream>
#include <stdexcept>
#include <string>
import phoenix.tools;
import phoenix.presentation;

// Explicitly invoked visual authoring journey; never registered with CTest.
int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: phoenix_layout_probe <disposable-pack-or-source-folder>\n";
		return 2;
	}
	try
	{
		phoenix::Application app;
		app.screen = phoenix::Screen::workshop;
		phoenix::open_window();
		phoenix::begin_tools();
		phoenix::open_workshop_pack(argv[1]);
		SetWindowSize(1600, 1000);
		SetTargetFPS(60);
		for (int frame = 0; frame < 240 && !WindowShouldClose(); ++frame)
		{
			if (frame == 5 || frame == 30 || frame == 60 || frame == 95 || frame == 130 || frame == 165 ||
			    frame == 200 || frame == 235)
			{
				const unsigned stage = frame == 5     ? 0
				                       : frame == 30  ? 1
				                       : frame == 60  ? 2
				                       : frame == 95  ? 3
				                       : frame == 130 ? 4
				                       : frame == 165 ? 5
				                       : frame == 200 ? 6
				                                      : 7;
				if (!phoenix::probe_workshop_layout(stage))
					throw std::runtime_error("Layout authoring probe failed");
			}
			BeginDrawing();
			phoenix::draw_screen(app);
			phoenix::draw_workshop(app);
			EndDrawing();
			if (frame == 20 || frame == 80 || frame == 115 || frame == 150 || frame == 185 || frame == 220)
				TakeScreenshot(("build/layout-workshop-" + std::to_string(frame) + ".png").c_str());
		}
		phoenix::end_tools();
		phoenix::close_window();
		std::cout << "PASS disposable layout edit, pack round trip, phone and theme preview\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
