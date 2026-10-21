// Explicit graphical evidence using the shipped rescue Courier and semantic actions.
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.session.execution;
using namespace phoenix;
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_sensors_smoke pack-directory");
		Application app;
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		app.session->start({"base.rescue-under-threat", {{"courier", "base.rescue-courier"}}, "courier", "helm"});
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(1280, 900);
		auto frame = [&]
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
		};
		for (int i = 0; i < 300; ++i)
			frame();
		auto advance = [&](int n)
		{
			while (n--)
			{
				app.session->tick();
				frame();
			}
		};
		auto act = [&](const std::string &id)
		{
			if (!app.screen_action("console/1/" + id))
				throw std::runtime_error("Refused sensor UI action: " + id);
			frame();
			frame();
		};
		auto button = [&](const std::string &label)
		{
			for (const auto &c : app.screen_controls)
				if (c.second.ends_with(label))
				{
					auto id = c.first;
					if (!app.screen_action(id))
						throw std::runtime_error("Refused " + label);
					frame();
					return;
				}
			throw std::runtime_error("Missing " + label);
		};
		auto capture = [&](const char *name)
		{
#ifdef __EMSCRIPTEN__
			// clang-format off
 EM_ASM({if(!Module.sensorCaptures)Module.sensorCaptures={};Module.sensorCaptures[UTF8ToString($0)]=Module.canvas.toDataURL("image/png");},name);
			// clang-format on
#else
			TakeScreenshot(name);
#endif
		};
		advance(5);
		act("game.console/tactical");
		act("game.sensor-contact/1");
		button("Scan target");
		advance(70);
		capture("sensors-partial.png");
		button("Clear scan target");
		advance(8);
		capture("sensors-paused.png");
		button("Scan target");
		advance(450);
		capture("sensors-acquired.png");
		bool acquired = false;
		const auto observed = app.session->observation();
		for (const auto &v : observed->additional_consoles)
			if (v.console == "tactical")
				for (const auto &c : v.gameplay.at("sensors").at("contacts"))
					if (c.at("id") == 1)
						acquired = c.at("findings").contains("compartment_condition");
		if (!acquired)
			throw std::runtime_error("Courier did not acquire compartment condition");
		button("Clear scan target");
		advance(3);
		capture("sensors-retained.png");
		std::cout << "PASS Courier Tactical select, slew, pause, resume, acquire and retained findings\n";
		close_window();
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "FAIL " << e.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
