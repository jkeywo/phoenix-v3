#include "../support/assertions.hpp"
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
import phoenix.navigation;
import phoenix.presentation;
import phoenix.session.local;
using namespace phoenix;
using phoenix::test::require;
namespace
{
void capture(const std::string &name)
{
#ifdef __EMSCRIPTEN__
	// clang-format off: embedded JavaScript.
	EM_ASM({
		if (!Module.cameraCaptures) Module.cameraCaptures = {};
		Module.cameraCaptures[UTF8ToString($0)] = Module.canvas.toDataURL("image/png");
	}, name.c_str());
	// clang-format on
#else
	TakeScreenshot(name.c_str());
#endif
}
} // namespace
int main(int argc, char **argv)
{
	bool opened = false;
	Application app;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_camera_smoke pack-directory [portrait]");
		app.can_host = app.can_edit = app.native_menu = true;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		app.session->add_pack(read_file(std::string(argv[1]) + "/training.zip"));
		app.session->start(
		    {"training.navigation", {{"training.player", "training.survey"}}, "training.player", "main"});
		app.screen = Screen::console;
		open_window();
		opened = true;
		if (argc > 2)
			SetWindowSize(1080, 1920);
		const auto frame = [&]
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
		};
		frame();
		frame();
		const auto action = [&](const std::string &name)
		{
			require(app.screen_action && app.screen_action("console/1/" + name), "camera widget action is admitted");
		};
		action("game.camera");
		action("game.right");
		action("game.contacts");
		action("game.camera-back");
		app.session->tick();
		frame();
		require(ship_camera_state(app.session->public_board()).mode == 1, "gamepad activates the focused choice");
		for (int mode = 0; mode < 2; ++mode)
			for (int direction = 0; direction < 8; ++direction)
			{
				app.screen = Screen::console;
				frame();
				frame();
				action("camera/camera-mode/" + std::to_string(mode));
				action("camera/camera-direction/" + std::to_string(direction));
				app.session->tick();
				frame();
				require(ship_camera_state(app.session->public_board()) == ShipCameraState{mode, direction},
				        "rendered UI uses accepted shared selection");
				require(std::count_if(app.screen_controls.begin(), app.screen_controls.end(),
				                      [](const auto &control)
				                      {
					                      return control.first.find("/camera/") != std::string::npos &&
					                             control.second.ends_with(". Selected");
				                      }) == 2,
				        "semantic controls identify the accepted mode and direction");
				for (const auto &[name, bounds] : app.control_placements)
					if (name.find("/camera/") != std::string::npos)
						require(bounds.width > 0 && bounds.height > 0 && bounds.x >= 0 && bounds.y >= 0 &&
						            bounds.x + bounds.width <= GetScreenWidth() + 1 &&
						            bounds.y + bounds.height <= GetScreenHeight() + 1,
						        "camera controls have usable viewport bounds");
				for (const auto &[first, a] : app.control_placements)
					for (const auto &[second, b] : app.control_placements)
						if (first < second && first.find("/camera/") != std::string::npos &&
						    second.find("/camera/") != std::string::npos)
							require(std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x) < 1 ||
							            std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y) < 1,
							        "camera choices do not overlap");
				if (direction == 0 && mode == 1)
					capture("camera-console.png");
				app.screen = Screen::viewscreen;
				frame();
				frame();
				capture("camera-" + std::to_string(mode) + "-" + std::to_string(direction) + ".png");
			}
		app.screen = Screen::console;
		frame();
		frame();
		action("settings");
		frame();
		require(!app.screen_action("console/1/camera/camera-mode/0"), "hidden camera controls cannot bypass a modal");
#ifndef __EMSCRIPTEN__
		action("local-settings.close");
#endif
		close_window();
		opened = false;
		std::cout << "PASS camera widgets, gamepad navigation, modal admission and 16 camera renders\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
