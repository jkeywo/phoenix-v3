// Explicitly invoked presentation smoke; injected input is not physical-device evidence.
#include "../support/assertions.hpp"
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <iostream>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>
import phoenix.navigation;
import phoenix.presentation;
import phoenix.session.local;
import phoenix.console;
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
		// Manually invoked smoke: use the real Helm pointer probe after setting
		// the same throttle value, so the dial changes heading without a speed write.
		ConsoleModel flight;
		flight.refresh(app.session);
		phoenix::test::require(flight.set_speed_fraction(.75), "establish steady flight");
		for (int i = 0; i < 80; ++i)
		{
			app.session->tick();
			frame();
		}
		flight.refresh(app.session);
		const auto before = flight.observation()->own;
		const auto speed_revision = [&]() -> std::uint64_t
		{
			for (const auto &owner : flight.observation()->gameplay.at("controllers"))
				for (const auto &command : owner.at("commands"))
					if (command.at("action") == "speed_mm_s")
						return command.at("revision");
			throw std::runtime_error("Helm speed channel missing");
		};
		const auto revision = speed_revision();
		phoenix::test::require(probe_console_helm(app, 1), "heading dial pointer input");
		for (int i = 0; i < 100; ++i)
		{
			app.session->tick();
			frame();
		}
		flight.refresh(app.session);
		const auto after = flight.observation()->own;
		phoenix::test::require(after.heading == 90000 && after.vy > before.vy && after.vx < before.vx &&
		                           speed_revision() == revision,
		                       "heading-only turn redirects flight and damps drift with steady throttle");
#ifndef __EMSCRIPTEN__
		TakeScreenshot((std::string(argv[1]) + "/steady-throttle-turn.png").c_str());
#endif
		phoenix::test::require(
		    flight.set_speed_fraction(-double(flight.system_minimum(flight.widget_system("throttle"))) /
		                              (flight.system_maximum(flight.widget_system("throttle")) -
		                               flight.system_minimum(flight.widget_system("throttle")))),
		    "zero throttle requests passive braking");
		for (int i = 0; i < 100; ++i)
		{
			app.session->tick();
			frame();
		}
		flight.refresh(app.session);
		phoenix::test::require(flight.observation()->own.vx == 0 && flight.observation()->own.vy == 0,
		                       "zero throttle passively brakes to rest");
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
		std::cout << "PASS steady-throttle heading turn, passive braking, manual Helm mode, captured joystick, "
		             "keyboard, analogue input and neutral release\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
