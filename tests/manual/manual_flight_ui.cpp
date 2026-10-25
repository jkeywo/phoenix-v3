// Explicitly invoked presentation smoke; injected input is not physical-device evidence.
#include "../support/assertions.hpp"
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <iostream>
#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>
import phoenix.navigation;
import phoenix.presentation;
import phoenix.session.local;
using namespace phoenix;
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_manual_flight_smoke pack-directory [portrait]");
		Application app;
		app.can_host = app.native_menu = true;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		GameSetup setup{"base.rescue-under-threat", {{"courier", "base.rescue-courier"}}, "courier", "helm"};
		setup.console_profiles["courier"]["helm"] = "detailed";
		app.session->start(setup);
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
		for (int i = 0; i < 300; ++i)
			frame();
		for (unsigned stage = 0; stage < 8; ++stage)
		{
			phoenix::test::require(probe_console_manual(1, stage),
			                       ("manual flight presentation stage " + std::to_string(stage)).c_str());
			app.session->tick();
			frame();
			if (stage == 1)
			{
				frame();
#ifdef __EMSCRIPTEN__
				// clang-format off: embedded JavaScript.
				EM_ASM({ Module.manualFlightCapture = Module.canvas.toDataURL("image/png"); });
				// clang-format on
#else
				TakeScreenshot((std::string(argv[1]) + "/manual-flight-console.png").c_str());
#endif
			}
		}
		phoenix::test::require(app.screen_action("console/1/settings"), "Console settings open through ordinary UI");
		frame();
		phoenix::test::require(!app.screen_action("console/1/manual-mode") &&
		                           !app.screen_action("console/1/manual-forward"),
		                       "settings refuse hidden manual mode and pulse actions");
		close_window();
		std::cout << "PASS manual Helm mode, captured joystick, keyboard, analogue input and neutral release\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
