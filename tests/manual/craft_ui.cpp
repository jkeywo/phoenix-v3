// Explicitly invoked graphical carrier journey, excluded from automatic tests.
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <fstream>
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
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_craft_smoke pack-directory [ship]");
		const bool manual = argc > 2 && std::string(argv[2]) == "ship";
		Application app;
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/test.zip"));
		app.session->start({"test.carrier.exercise", {{"carrier", "test.carrier.ship"}}, "carrier", "helm"});
		const auto ship = app.session->observation()->own.id;
		if (!app.session->claim_console(2, {ship, "support"}))
			throw std::runtime_error("Engineering claim failed");
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(2560, 900);
		SetTargetFPS(0);
		app.console_regions = {{1, 0, 0, 1280, 900}, {2, 1280, 0, 1280, 900}};
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
				app.session->tick();
			frame();
			frame();
		};
		auto capture = [&](const std::string &name)
		{
			frame();
			TakeScreenshot(("build/carrier-" + name + ".png").c_str());
		};
		auto action = [&](const std::string &id)
		{
			for (const auto &c : app.screen_controls)
				if (c.first == id || c.second == id)
				{
					const auto key = c.first;
					if (app.focus_control)
						app.focus_control(key);
					frame();
					frame();
					if (key.find("craft/bomb/") != std::string::npos)
						capture("bomber-controls");
					if (app.screen_action(key))
					{
						advance(2);
						return;
					}
				}
			std::ofstream("build/carrier-ui-controls.json") << Json(app.screen_controls).dump(2);
			std::ofstream("build/carrier-ui-observations.json")
			    << app.session->console_observation(2)->gameplay.dump(2);
			throw std::runtime_error("Missing/refused UI action: " + id);
		};
		auto mission = [&]
		{
			return app.session->console_observation(1)->gameplay.at("away_missions").at(0);
		};
		auto crafts = [&]
		{
			return app.session->console_observation(1)->gameplay.at("craft");
		};
		auto until = [&](auto predicate, const char *why)
		{
			for (int i = 0; i < 3000; ++i)
			{
				if (predicate())
				{
					frame();
					return;
				}
				app.session->tick();
			}
			std::ofstream("build/carrier-ui-state.json") << app.session->save().size();
			throw std::runtime_error(std::string("Timed out: ") + why + " " + mission().dump() + " " + crafts().dump());
		};
		for (int i = 0; i < 3000; ++i)
		{
			frame();
			if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                [](const auto &c)
			                {
				                return c.first == "console/1/away";
			                }))
				break;
			WaitTime(0.01);
		}
		advance(35);
		capture("helm");
		action("console/1/away");
		action("console/1/away/open/repair/");
		action("console/2/away");
		action("console/2/away/assign/repair/engineers/team:team");
		std::vector<std::string> shuttles;
		std::string interceptor, bomber;
		for (const auto &c : crafts())
		{
			const auto id = c.at("id").get<std::string>();
			if (c.at("role") == "shuttle")
				shuttles.push_back(id);
			if (c.at("role") == "interceptor")
				interceptor = id;
			if (c.at("role") == "bomber")
				bomber = id;
		}
		action("console/1/away/transport/repair/" + (manual ? std::string("ship") : "craft:" + shuttles.at(0)));
		capture(manual ? "ship-assigned" : "shuttle-assigned");
		if (manual)
		{
			action("console/1/away/send/repair/");
			if (mission().at("phase") != "open")
				throw std::runtime_error("Ship delivered remotely");
			action("console/1/local-settings.close");
			// Manual Helm widget commands, without issuing navigation or movement directly.
			ConsoleModel helm;
			helm.refresh(app.session, 1);
			if (!helm.set_heading_degrees(20.556, helm.widget_system("steering")))
				throw std::runtime_error("Heading widget refused");
			advance(40);
			helm.refresh(app.session, 1);
			if (!helm.set_speed_fraction(.35, helm.widget_system("engine")))
				throw std::runtime_error("Throttle widget refused");
			until(
			    [&]
			    {
				    auto o = app.session->console_observation(1)->own;
				    auto dx = o.x - 80000, dy = o.y - 30000;
				    return dx * dx + dy * dy < 12000LL * 12000;
			    },
			    "manual ship approach");
			helm.refresh(app.session, 1);
			if (!helm.set_speed_fraction(0, helm.widget_system("engine")))
				throw std::runtime_error("Stop widget refused");
			advance(30);
			capture("ship-alongside");
			action("console/1/away");
			action("console/1/away/send/repair/");
			until(
			    [&]
			    {
				    return mission().at("phase") == "deployed";
			    },
			    "manual ship delivery");
			capture("ship-deployed");
			advance(200);
			action("console/1/away/recall/repair/");
			until(
			    [&]
			    {
				    return mission().at("phase") == "recovered";
			    },
			    "manual ship pickup");
			capture("ship-recovered");
			std::cout << "PASS manual ship UI range gate, Helm approach, delivery and recovery\n";
			close_window();
			opened = false;
			return 0;
		}
		action("console/1/away/send/repair/");
		until(
		    [&]
		    {
			    return mission().at("phase") == "deployed";
		    },
		    "shuttle delivery");
		capture("deployed");
		advance(200);
		until(
		    [&]
		    {
			    for (const auto &c : crafts())
				    if (c.at("id") == shuttles[0])
					    return c.at("available").get<bool>();
			    return false;
		    },
		    "released shuttle return");
		action("console/1/away/transport/repair/craft:" + shuttles.at(1));
		action("console/1/away/recall/repair/");
		until(
		    [&]
		    {
			    return mission().at("phase") == "recovered";
		    },
		    "replacement shuttle recovery");
		capture("recovered");
		action("console/1/local-settings.close");
		action("console/1/settings");
		action("console/1/local-settings.tab/craft");
		capture("flight-operations");
		action("console/1/craft/defend/" + interceptor + "/");
		advance(40);
		capture("interceptor");
		action("console/1/craft/escort/" + interceptor + "/" + std::to_string(ship));
		std::int64_t bomber_rounds = 0;
		for (const auto &c : crafts())
			if (c.at("id") == bomber)
				bomber_rounds = c.at("ammunition");
		action("console/1/craft/bomb/" + bomber + "/2");
		advance(10);
		capture("sorties");
		bool bomber_fired = false;
		until(
		    [&]
		    {
			    for (const auto &c : crafts())
				    if (c.at("id") == bomber)
				    {
					    bomber_fired |= c.at("ammunition").get<std::int64_t>() < bomber_rounds;
					    return c.at("phase") == "docked";
				    }
			    return false;
		    },
		    "bomber return");
		if (!bomber_fired)
			throw std::runtime_error("Bomber returned without expending its missile salvo");
		action("console/1/craft/recall/" + interceptor + "/");
		until(
		    [&]
		    {
			    for (const auto &c : crafts())
				    if (c.at("id") == interceptor)
					    return c.at("phase") == "docked";
			    return false;
		    },
		    "interceptor return");
		advance(200);
		capture("serviced");
		action("console/1/craft/defend/" + interceptor + "/");
		advance(40);
		action("console/1/local-settings.close");
		action("Turn FTL battery on");
		advance(3);
		capture("ftl-recall");
		action("console/1/settings");
		action("console/1/local-settings.tab/craft");
		until(
		    [&]
		    {
			    for (const auto &c : crafts())
				    if (c.at("id") == interceptor)
					    return c.at("phase") == "docked";
			    return false;
		    },
		    "FTL recall");
		capture("ftl-recovered");
		std::ofstream("build/carrier-ui-readouts.json") << Json(app.screen_readouts).dump(2);
		std::cout << "PASS carrier UI delivery, release, replacement pickup, defence, escort, bombing and servicing\n";
		close_window();
		opened = false;
	}
	catch (const std::exception &e)
	{
		if (opened)
		{
			TakeScreenshot("build/carrier-failure.png");
			close_window();
		}
		std::cerr << "FAIL " << e.what() << '\n';
		return 1;
	}
}
