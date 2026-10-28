#include <raylib.h>
#include <iostream>
#include <stdexcept>
#include <string>
import phoenix.tools;
import phoenix.presentation;
import phoenix.navigation;

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
		for (int frame = 0; frame < 710 && !WindowShouldClose(); ++frame)
		{
			const int stages[] = {5,   30,  60,  95,  130, 165, 200, 235, 270, 305, 340,
			                      375, 410, 445, 480, 510, 540, 575, 605, 635, 675};
			for (unsigned stage = 0; stage < 21; ++stage)
				if (frame == stages[stage] && !phoenix::probe_workshop_layout(stage))
					throw std::runtime_error("Layout authoring probe failed at stage " + std::to_string(stage));
			BeginDrawing();
			phoenix::draw_screen(app);
			phoenix::draw_workshop(app);
			EndDrawing();
			if (frame == 20 || frame == 80 || frame == 115 || frame == 150 || frame == 185 || frame == 220 ||
			    frame == 255 || frame == 290 || frame == 325 || frame == 360 || frame == 500 || frame == 530 ||
			    frame == 625 || frame == 660 || frame == 700)
				TakeScreenshot(("build/layout-workshop-" + std::to_string(frame) + ".png").c_str());
		}
		phoenix::end_tools();
		phoenix::close_window();
		std::cout << "PASS disposable layout edit, pack round trip, phone tabs, theme preview, RmlUi wrapping and "
		             "circle-cutout geometry, registered Power/Repair pages and capability filtering\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
