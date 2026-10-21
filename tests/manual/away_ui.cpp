// Explicitly invoked graphical rescue journey; never part of automatic ctest.
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.session.execution;
import phoenix.console;
using namespace phoenix;
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_away_smoke pack-directory");
		Application app;
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		app.session->start({"base.rescue-under-threat", {{"courier", "base.rescue-courier"}}, "courier", "helm"});
		const auto ship = app.session->observation()->own.id;
		if (!app.session->claim_console(2, {ship, "support"}))
			throw std::runtime_error("Engineering claim failed");
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(2560, 900);
		app.console_regions = {{1, 0, 0, 1280, 900}, {2, 1280, 0, 1280, 900}};
		auto frame = [&]
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
		};
		for (int i = 0; i < 200; ++i)
			frame();
		auto advance = [&](int n)
		{
			for (int i = 0; i < n; ++i)
			{
				app.session->tick();
				frame();
			}
		};
		auto action = [&](const std::string &id)
		{
			for (const auto &c : app.screen_controls)
				if (c.first == id)
				{
					auto copy = c.first;
					if (app.screen_action(copy))
						return;
				}
			std::cerr << app.session->console_observation(1)->gameplay.at("away_missions").dump() << '\n';
			throw std::runtime_error("Missing/refused UI action: " + id);
		};
		advance(35);
		action("console/1/personnel");
		advance(2);
		TakeScreenshot("away-navigator.png");
		action("console/1/local-settings.close");
		advance(1);
		action("console/1/away");
		advance(2);
		action("console/1/away/open/repairs/");
		advance(2);
		action("console/2/away");
		advance(2);
		TakeScreenshot("away-assignments.png");
		action("console/2/away/assign/repairs/repair-team/team:team-1");
		advance(2);
		action("console/1/away/transport/repairs/displacement");
		advance(2);
		ConsoleModel engineering;
		engineering.refresh(app.session, 2);
		for (const auto *key : {"shield-fore", "shield-aft"})
		{
			bool accepted = false;
			const auto observed = app.session->console_observation(2);
			for (const auto &control : observed->gameplay.at("controllers"))
				if (control.at("id") == key)
				{
					OperatingCommand cmd{ship, {}, 0, {}};
					cmd.console = "support";
					cmd.controller = key;
					cmd.channel = "enabled";
					cmd.generation = control.at("generation");
					accepted = app.session->console_command(2, cmd);
				}
			if (!accepted)
				throw std::runtime_error(std::string("Shield command failed: ") + key);
		}
		advance(150);
		TakeScreenshot("away-manifest.png");
		action("console/1/away/send/repairs/");
		advance(80);
		if (app.session->console_observation(1)->gameplay.at("away_missions")[0].at("phase") != "deployed")
			throw std::runtime_error("Team did not deploy");
		TakeScreenshot("away-deployed.png");
		action("console/2/local-settings.close");
		advance(1);
		action("console/2/game.console/comms");
		advance(3);
		std::string select;
		for (const auto &control : app.screen_controls)
			if (control.first.starts_with("console/2/comms/select/"))
			{
				select = control.first;
				break;
			}
		if (!select.empty())
		{
			action(select);
			advance(3);
		}
		TakeScreenshot("away-before-choice.png");
		// The response is authored and routed through the existing Comms UI.
		action("console/2/comms/answer/stabilise");
		advance(10);
		TakeScreenshot("away-comms.png");
		action("console/1/away/recall/repairs/");
		advance(65);
		if (app.session->console_observation(1)->gameplay.at("away_missions")[0].at("phase") != "recovered")
			throw std::runtime_error("Team did not recover");
		TakeScreenshot("away-recovered.png");
		// Resume with the same party and finish the authored services/propulsion route.
		action("console/2/game.console/support");
		advance(3);
		action("console/2/away");
		advance(2);
		action("console/2/away/assign/repairs/repair-team/team:team-1");
		advance(4);
		action("console/1/away/send/repairs/");
		advance(1400);
		action("console/1/away/recall/repairs/");
		advance(65);
		const auto completed = app.session->console_observation(1)->gameplay.at("away_missions")[0];
		if (completed.at("phase") != "recovered" || completed.at("result") == "")
			throw std::runtime_error("Resumed mission did not complete and recover");
		TakeScreenshot("away-complete.png");
		close_window();
		opened = false;
		std::cout << "PASS rescue assignment, deployment, Comms decision, recovery, resume and completion UI smoke\n";
	}
	catch (const std::exception &e)
	{
		if (opened)
			close_window();
		std::cerr << e.what() << '\n';
		return 1;
	}
}
