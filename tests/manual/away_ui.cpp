// Explicitly invoked graphical rescue journey; never part of automatic ctest.
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.navigation;
import phoenix.session.execution;
import phoenix.console;
using namespace phoenix;
int main(int argc, char **argv)
{
	Application app;
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_away_smoke pack-directory");
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		auto draft = read_pack(read_file(std::string(argv[1]) + "/base.zip")).draft();
		auto definitions = parse_json(draft.files.at("definitions.json"), "clinical UI fixture");
		for (auto &entity : definitions["entities"])
			if (entity["id"] == "base.rescue-courier")
				entity["personnel"]["officers"][0]["condition"] = "minor";
		draft.files["definitions.json"] = json_bytes(definitions);
		app.session->add_pack(write_pack(draft));
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
		bool ready = false;
		for (int i = 0; i < 600 && !ready; ++i)
		{
			frame();
			ready = std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                    [](const auto &c)
			                    {
				                    return c.first == "console/2/personnel";
			                    });
		}
		if (!ready)
			throw std::runtime_error("Console assets did not finish loading");
		auto advance = [&](int n)
		{
			for (int i = 0; i < n; ++i)
			{
				app.session->tick();
				if (i % 20 == 0 || i == n - 1)
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
			std::cerr << "Clinical teams: " << app.session->console_observation(2)->gameplay.at("teams").dump() << "\n";
			for (const auto &c : app.screen_controls)
				if (c.first.starts_with("console/2/"))
					std::cerr << c.first << "\n";
			std::cerr << app.session->console_observation(1)->gameplay.at("away_missions").dump() << '\n';
			throw std::runtime_error("Missing/refused UI action: " + id);
		};
		advance(35);
		action("console/2/game.teams");
		advance(1);
		action("console/2/game.team-order/medical-team/treat");
		advance(1);
		TakeScreenshot("away-medical-map.png");
		ConsoleModel clinical;
		clinical.refresh(app.session, 2);
		const auto statuses = clinical.system_statuses();
		if (std::none_of(statuses.begin(), statuses.end(),
		                 [](const auto &s)
		                 {
			                 return s.semantic_label.find("Officer injured") != std::string::npos;
		                 }))
			throw std::runtime_error("Injured duty officer lacks medical status overlay");
		for (const auto &compartment : clinical.team_map())
			if (compartment["id"] == "stern-port")
				action("console/2/game.compartment/" + std::to_string(compartment.at("index").get<int>()));
		advance(2);
		action("console/2/game.team-cancel/medical-team");
		advance(1);
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
		advance(1);
		action("console/2/away/assign/repairs/medical-team/team:medical-team");
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
		advance(1);
		action("console/2/away/assign/repairs/medical-team/team:medical-team");
		advance(4);
		action("console/1/away/send/repairs/");
		action("console/2/local-settings.close");
		advance(1);
		action("console/2/game.console/comms");
		advance(3);
		// Wait for the authored work and reports rather than fixing shipped tuning.
		auto work_reported = [&]
		{
			const auto view = app.session->console_observation(2);
			const Observation *comms = &*view;
			for (const auto &extra : view->additional_consoles)
				if (extra.console == "comms")
					comms = &extra;
			for (const auto &thread : comms->gameplay.at("conversations").at("threads"))
				for (const auto &message : thread.at("messages"))
					if (message.at("text") == "Repairs complete. The team is ready for recovery.")
						return true;
			return false;
		};
		for (int i = 0; i < 600 && !work_reported(); ++i)
			advance(10);
		if (!work_reported())
			throw std::runtime_error("Resumed work did not report completion");
		action("console/1/away/recall/repairs/");
		advance(65);
		const auto completed = app.session->console_observation(1)->gameplay.at("away_missions")[0];
		if (completed.at("phase") != "recovered" || completed.at("result") == "")
			throw std::runtime_error("Resumed mission did not complete and recover");
		TakeScreenshot("away-complete.png");
		close_window();
		opened = false;
		std::cout << "PASS Medical dispatch map, officer indicator, optional clinical deployment, Comms, recovery and "
		             "completion UI smoke\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
