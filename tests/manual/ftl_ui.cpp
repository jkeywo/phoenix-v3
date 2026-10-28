// Explicitly invoked rendered Navigator journey, never an automatic ctest case.
#include <raylib.h>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.navigation;
import phoenix.session.execution;
using namespace phoenix;
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_ftl_smoke pack-directory");
		Application app;
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		app.session->start({"base.free-flight", {{"base.player", "base.courier-engineering"}}, "base.player", "helm"});
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(1280, 900);
		app.console_regions = {{1, 0, 0, 1280, 900}};
		auto frame = [&]
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
		};
		auto advance = [&](int n)
		{
			for (int i = 0; i < n; ++i)
			{
				app.session->tick();
				frame();
			}
		};
		auto action = [&](const std::string &wanted)
		{
			for (const auto &[id, label] : app.screen_controls)
				if (id == wanted || label == wanted)
				{
					const auto copy = id;
					if (app.screen_action(copy))
						return;
				}
			for (const auto &[id, label] : app.screen_controls)
				std::cerr << id << " = " << label << '\n';
			std::cerr << "Observation: " << app.session->console_observation(1)->gameplay.at("controllers").dump()
			          << '\n';
			throw std::runtime_error("Missing/refused GUI action: " + wanted);
		};
		for (int i = 0; i < 200; ++i)
			frame();
		advance(5);
		action("console/1/game.console/navigator");
		advance(1);
		TakeScreenshot("build/ftl-off.png");
		action("Turn FTL battery on");
		advance(300);
		TakeScreenshot("build/ftl-charging.png");
		auto state = [&]() -> Json
		{
			const auto o = app.session->console_observation(1);
			if (!o->gameplay.value("ftl", Json::array()).empty())
				return o->gameplay.at("ftl").at(0);
			for (const auto &c : o->additional_consoles)
				if (c.console == "navigator")
					return c.gameplay.at("ftl").at(0);
			throw std::runtime_error("Navigator projection missing");
		};
		for (int i = 0; i < 1800 && !state().at("ready").get<bool>(); ++i)
			advance(1);
		if (!state().at("ready").get<bool>())
			throw std::runtime_error("Authored FTL never became ready");
		advance(20);
		TakeScreenshot("build/ftl-ready.png");
		action("Leave mission");
		advance(2);
		if (app.session->console_observation(1)->gameplay.at("mission_report").is_null())
			throw std::runtime_error("Crew report missing after departure");
		bool report = false;
		for (const auto &[id, text] : app.screen_readouts)
			report |= id == "console/1/mission-result" && text.find("Departed via FTL") != std::string::npos;
		if (!report)
			throw std::runtime_error("Rendered report semantics missing");
		TakeScreenshot("build/ftl-report.png");
		close_window();
		opened = false;
		std::cout << "PASS Navigator GUI battery, charging, readiness, departure and crew report\n";
	}
	catch (const std::exception &e)
	{
		if (opened)
			close_window();
		std::cerr << e.what() << '\n';
		return 1;
	}
}
