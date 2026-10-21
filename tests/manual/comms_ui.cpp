// Manually invoked graphical journey through the authored Comms demonstration.
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <fstream>
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
			throw std::runtime_error("Usage: phoenix_comms_smoke pack-directory");
		Application app;
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		app.session->start({"base.comms-demonstration",
		                    {{"courier", "base.comms-courier"}, {"partner", "base.comms-courier"}},
		                    "courier",
		                    "engineering"});
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(1280, 900);
		const auto frame = [&]()
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
		};
		for (int i = 0; i < 300; ++i)
			frame();
		const auto action = [&](const std::string &suffix)
		{
			for (const auto &c : app.screen_controls)
				if (c.first.ends_with(suffix))
				{
					auto id = c.first;
					return app.screen_action(id);
				}
			return false;
		};
		if (!action("game.console/comms"))
			throw std::runtime_error("Comms Console switch unavailable");
		for (int i = 0; i < 3; ++i)
		{
			app.session->tick();
			frame();
		}
		if (!action("comms/hail/ship:1/captain"))
		{
			for (const auto &c : app.screen_controls)
				std::cerr << c.first << '\n';
			throw std::runtime_error("Public captain hail unavailable");
		}
		for (int i = 0; i < 35; ++i)
		{
			app.session->tick();
			frame();
		}
		TakeScreenshot("comms-incoming.png");
		for (int i = 0; i < 40; ++i)
		{
			app.session->tick();
			frame();
		}
		TakeScreenshot("comms-choices.png");
		if (!action("comms/answer/help"))
			throw std::runtime_error("Authored response unavailable");
		for (int i = 0; i < 80; ++i)
		{
			app.session->tick();
			frame();
		}
		TakeScreenshot("comms-group.png");
		if (!action("comms/screen"))
			throw std::runtime_error("On Screen unavailable");
		app.session->tick();
		frame();
		const auto shared = app.session->public_board().at("display");
		if (shared.at("kind") != "conversations" || shared.at("conversation").contains("choices"))
			throw std::runtime_error("Read-only conversation publication failed");
		const auto &members = shared.at("conversation").at("members");
		if (std::none_of(members.begin(), members.end(),
		                 [](const auto &p)
		                 {
			                 return p.at("id") == "ship:1/engineer";
		                 }))
			throw std::runtime_error("Scripted engineer did not join");
		app.screen = Screen::viewscreen;
		for (int i = 0; i < 3; ++i)
			frame();
		if (std::none_of(app.screen_readouts.begin(), app.screen_readouts.end(),
		                 [](const auto &r)
		                 {
			                 return r.first.starts_with("viewscreen.conversation/");
		                 }))
			throw std::runtime_error("Shared transcript semantic rows missing");
		TakeScreenshot("comms-viewscreen.png");
		app.screen = Screen::console;
		frame();
		if (!action("game.console/comms"))
			throw std::runtime_error("Comms return unavailable");
		SetWindowSize(600, 900);
		for (int i = 0; i < 3; ++i)
			frame();
		TakeScreenshot("comms-narrow.png");
		app.session->tick();
		std::ofstream("comms-state.json")
		    << parse_json(read_pack(app.session->save()).files().at("session.json"), "smoke state").at("state").dump(2);
		close_window();
		opened = false;
		std::cout << "PASS authored Comms hail, incoming, choice, NPC join, public display and narrow-layout smoke\n";
	}
	catch (const std::exception &e)
	{
		if (opened)
			close_window();
		std::cerr << e.what() << '\n';
		return 1;
	}
}
