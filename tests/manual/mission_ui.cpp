// Explicitly invoked graphical rescue journey; never part of automatic ctest.
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cmath>
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
			throw std::runtime_error("Usage: phoenix_mission_smoke pack-directory");
		const std::string mode = argc > 2 ? argv[2] : "success";
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
		SetTargetFPS(0);
		app.console_regions = {{1, 0, 0, 1280, 900}, {2, 1280, 0, 1280, 900}};
		auto frame = [&](bool capture = false)
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
#ifdef __EMSCRIPTEN__
			if (capture)
				EM_ASM({ Module.missionCaptures = {debrief : Module.canvas.toDataURL("image/png")};});
			emscripten_sleep(0);
#endif
		};
		for (int i = 0; i < 200; ++i)
		{
			frame();
			if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                [](const auto &c)
			                {
				                return c.first == "console/1/personnel";
			                }))
				break;
		}
		auto advance = [&](int n)
		{
			for (int i = 0; i < n; ++i)
			{
				app.session->tick();
				if (i == n - 1)
					frame();
			}
		};
		auto action = [&](const std::string &id)
		{
			for (const auto &c : app.screen_controls)
				if (c.first == id || c.second == id)
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
		action("console/1/local-settings.close");
		advance(1);
		action("console/1/away");
		advance(2);
		action("console/1/away/open/repairs/");
		advance(2);
		action("console/2/away");
		advance(2);
		action("console/2/away/assign/repairs/repair-team/team:team-1");
		advance(2);
		action("console/1/away/transport/repairs/displacement");
		advance(2);
		ConsoleModel engineering;
		engineering.refresh(app.session, 2);
		for (const auto *key : {"shield-fore", "shield-aft", "reactor"})
		{
			bool accepted = false;
			const auto observed = app.session->console_observation(2);
			for (const auto &control : observed->gameplay.at("controllers"))
				if (control.at("id") == key)
				{
					OperatingCommand cmd{ship, {}, 0, {}};
					cmd.console = "support";
					cmd.controller = key;
					cmd.channel = std::string(key) == "reactor" ? "output" : "enabled";
					if (std::string(key) == "reactor")
						cmd.value = control.at("commands").at("output").at("maximum");
					cmd.generation = control.at("generation");
					accepted = app.session->console_command(2, cmd);
				}
			if (!accepted)
				throw std::runtime_error(std::string("Shield command failed: ") + key);
		}
		advance(150);
		action("console/1/away/send/repairs/");
		advance(80);
		if (app.session->console_observation(1)->gameplay.at("away_missions")[0].at("phase") != "deployed")
			throw std::runtime_error("Team did not deploy");
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
		// The response is authored and routed through the existing Comms UI.
		action("console/2/comms/answer/stabilise");

		auto view = [&](std::uint64_t route, const std::string &console)
		{
			auto o = *app.session->console_observation(route);
			if (o.console == console)
				return o;
			for (const auto &extra : o.additional_consoles)
				if (extra.console == console)
					return extra;
			throw std::runtime_error("Missing assigned Console: " + console);
		};
		auto objective = [&](const std::string &title, const std::string &status)
		{
			ConsoleModel model;
			model.refresh(app.session, 1);
			for (const auto &o : model.objectives())
				if (o.at("title") == title && o.at("status") == status)
					return true;
			return false;
		};
		auto until = [&](auto done, const std::string &label)
		{
			std::cout << "WAIT " << label << std::endl;
#ifndef __EMSCRIPTEN__
			if (label == "casualty escape" || label == "casualty loss")
			{
				const auto draft = read_pack(app.session->save()).draft();
				const auto state = Json::parse(draft.files.at("session.json"));
				std::ofstream("build/mission-diagnostic.json") << state.at("state").dump(2);
			}
#endif
			const int batch = label == "firing position" || label == "stop at firing position" ? 10 : 50;
			for (int i = 0; i < 12000 && !done(); i += batch)
			{
				advance(batch);
				if (i % 1000 == 0)
				{
					ConsoleModel m;
					m.refresh(app.session, 1);
					std::cout << "PROGRESS " << label << " " << m.objectives().dump() << " "
					          << app.session->console_observation(1)->gameplay.at("away_missions").dump() << std::endl;
				}
			}
			if (!done())
				throw std::runtime_error("Timed out: " + label);
		};
		if (mode == "loss")
		{
			action("console/1/local-settings.close");
			advance(2);
			action("console/1/game.console/tactical");
			advance(2);
			const auto o = view(1, "tactical");
			for (const auto &c : o.gameplay.at("controllers"))
				for (auto channel = c.at("commands").begin(); channel != c.at("commands").end(); ++channel)
					if (channel.value().at("action") == "weapon_target")
					{
						OperatingCommand cmd{ship, {}, 1, {}};
						cmd.console = "tactical";
						cmd.controller = c.at("id");
						cmd.channel = channel.key();
						cmd.generation = c.at("generation");
						if (!app.session->console_command(1, cmd))
							throw std::runtime_error("Weapon targeting refused");
					}
			auto helmOrder = [&](const std::string &actionName, std::int64_t value)
			{
				auto helm = view(1, "helm");
				for (const auto &c : helm.gameplay.at("controllers"))
					for (auto channel = c.at("commands").begin(); channel != c.at("commands").end(); ++channel)
						if (channel.value().at("action") == actionName)
						{
							OperatingCommand command{ship, {}, value, {}};
							command.console = "helm";
							command.controller = c.at("id");
							command.channel = channel.key();
							command.generation = c.at("generation");
							if (!app.session->console_command(1, command))
								throw std::runtime_error("Helm manoeuvre refused");
							return;
						}
				throw std::runtime_error("Helm manoeuvre unavailable");
			};
			auto move = [&](std::int64_t heading, bool xAxis, std::int64_t destination)
			{
				helmOrder("heading_mdeg", heading);
				advance(100);
				helmOrder("speed_mm_s", 10000);
				until(
				    [&]
				    {
					    auto own = app.session->console_observation(1)->own;
					    auto coordinate = xAxis ? own.x : own.y;
					    return (heading == 180000 || heading == 270000) ? coordinate <= destination
					                                                    : coordinate >= destination;
				    },
				    "firing position");
				helmOrder("speed_mm_s", 0);
				until(
				    [&]
				    {
					    auto own = app.session->console_observation(1)->own;
					    return std::abs(own.vx) < 10 && std::abs(own.vy) < 10;
				    },
				    "stop at firing position");
				auto own = app.session->console_observation(1)->own;
				auto headingToTarget = static_cast<std::int64_t>(
				    std::llround(std::atan2(30000. - own.y, 90000. - own.x) * 180000. / 3.141592653589793));
				helmOrder("heading_mdeg", (headingToTarget + 360000) % 360000);
				advance(100);
			};
			// Fire across both sides using normal Helm and Tactical commands.
			move(0, true, 160000);
			advance(800);
			if (!objective("Secure the casualty escape", "failed"))
			{
				move(90000, false, 70000);
				advance(800);
			}
			if (!objective("Secure the casualty escape", "failed"))
			{
				move(180000, true, 10000);
				advance(800);
			}
			until(
			    [&]
			    {
				    return objective("Secure the casualty escape", "failed");
			    },
			    "casualty loss");
		}
		else
		{
			until(
			    [&]
			    {
				    return objective(mode == "partial" ? "Restore casualty services" : "Restore casualty propulsion",
				                     "completed");
			    },
			    "restoration");
			if (mode == "success")
				until(
				    [&]
				    {
					    return view(2, "comms").gameplay.at("conversations").dump().find("Repairs complete.") !=
					           std::string::npos;
				    },
				    "completed operation");
			action("console/1/away/recall/repairs/");
			until(
			    [&]
			    {
				    return app.session->console_observation(1)->gameplay.at("away_missions")[0].at("phase") ==
				           "recovered";
			    },
			    "party recovery");
			if (mode == "success")
				until(
				    [&]
				    {
					    return objective("Secure the casualty escape", "completed");
				    },
				    "casualty escape");
			if (mode == "success" &&
			    view(2, "comms").gameplay.at("conversations").dump().find("Our FTL reserve is ready.") ==
			        std::string::npos)
				throw std::runtime_error("Physical readiness announcement missing");
			action("console/1/local-settings.close");
			advance(2);
		}
		action("console/2/game.console/navigator");
		advance(2);
		action("Turn FTL battery on");
		advance(2);
		until(
		    [&]
		    {
			    return view(2, "navigator").gameplay.at("ftl")[0].at("ready").template get<bool>();
		    },
		    "Courier charge");
		action("Leave mission");
		advance(3);
		const auto report = app.session->console_observation(1)->gameplay.at("mission_report");
		const auto expected = mode == "loss" ? "failure" : mode;
		if (report.at("result") != expected)
			throw std::runtime_error("Unexpected debrief: " + report.dump());
		advance(20);
		for (const auto &[id, text] : app.screen_readouts)
			if (text == "Ship destroyed")
				throw std::runtime_error("Departure presented as destruction");
#ifdef __EMSCRIPTEN__
		frame(true);
#else
		TakeScreenshot(("build/mission-" + mode + ".png").c_str());
#endif
		std::cout << "DEBRIEF " << report.dump() << "\n";
		close_window();
		opened = false;
		std::cout << "PASS rescue " << mode << " graphical objectives, physical outcomes and authoritative debrief\n";
	}
	catch (const std::exception &e)
	{
		if (opened)
			close_window();
		std::cerr << "FAIL " << e.what() << "\n";
		return 1;
	}
}
