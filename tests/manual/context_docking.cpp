// Explicit synthetic native graphical journey, excluded from CTest.
#include "../support/assertions.hpp"
#include "../support/docking_fixture.hpp"
#include <raylib.h>
#include <algorithm>
#include <iostream>
import phoenix.presentation;
import phoenix.application;
import phoenix.console;
import phoenix.session.execution;
using namespace phoenix;
using namespace phoenix::test;
int main()
{
	try
	{
		auto source = docking_fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["radar"] = {{"kind", "radar"}, {"range_mm", 100000}, {"compartment", "bridge"}};
		ship["stations"][0]["consoles"][0]["feeds"].push_back("radar");
		for (auto &controller : ship["control_layout"]["controllers"])
			if (controller["id"] == "radio")
				controller["commands"][0]["consumers"].push_back("radar");
		auto observer = source["consoles"][0];
		observer["id"] = "test.observer";
		source["consoles"].push_back(observer);
		ship["stations"][1]["consoles"][0]["definition"] = "test.observer";
		auto &console = source["consoles"][0];
		console["name"] = "Helm";
		console["widgets"] = Json::array({{{"id", "radar"},
		                                   {"kind", "radar"},
		                                   {"label", "Radar"},
		                                   {"binding", "radar.contacts"},
		                                   {"system", "radar"},
		                                   {"x", 0},
		                                   {"y", 0},
		                                   {"width", 650},
		                                   {"height", 1000}},
		                                  {{"id", "engine"},
		                                   {"kind", "throttle"},
		                                   {"label", "Speed"},
		                                   {"binding", "engine.desired_speed"},
		                                   {"controller", "engine"},
		                                   {"channel", "speed"},
		                                   {"x", 650},
		                                   {"y", 0},
		                                   {"width", 150},
		                                   {"height", 1000}}});
		console["layout"] = {{"schema", 1},
		                     {"root",
		                      {{"id", "helm"},
		                       {"kind", "row"},
		                       {"children", Json::array({{{"id", "radar"},
		                                                  {"kind", "component"},
		                                                  {"component", "radar"},
		                                                  {"aspect", 1},
		                                                  {"min_width", 280},
		                                                  {"min_height", 280},
		                                                  {"stretch", 3}},
		                                                 {{"id", "throttle"},
		                                                  {"kind", "component"},
		                                                  {"component", "engine"},
		                                                  {"min_width", 72},
		                                                  {"min_height", 240}},
		                                                 {{"id", "target"},
		                                                  {"kind", "component"},
		                                                  {"component", "readouts"},
		                                                  {"label", "Contact info"},
		                                                  {"min_width", 300}}})}}}};
		Application app;
		app.can_host = true;
		app.session = make_local_session();
		app.session->add_pack(archive(source));
		app.session->start({"test.map", {{"crew", "test.away"}}, "crew", "drive"});
		app.screen = Screen::console;
		app.console_regions = {{1, 0, 0, 1920, 1080}};
		open_window();
		SetWindowSize(1920, 1080);
		SetTargetFPS(60);
		auto draw = [&]
		{
			BeginDrawing();
			draw_screen(app);
			EndDrawing();
		};
		auto frame = [&]
		{
			app.session->tick();
			draw();
		};
		auto has = [&](const std::string &key)
		{
			return std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                   [&](const auto &row)
			                   {
				                   return row.first == key;
			                   });
		};
		auto dock_button = [&](const std::string &label)
		{
			for (const auto &[key, text] : app.screen_controls)
				if (key.starts_with("console/1/contact-dock/") && text == label)
					return key.substr(10);
			return std::string{};
		};
		auto click = [&](const std::string &action)
		{
			require(!action.empty() && probe_console_tab(1, action), ("pointer clicks " + action).c_str());
			frame();
			draw();
			draw();
		};
		for (int i = 0; i < 1200 && !has("console/1/radar-contact/1"); ++i)
			frame();
		require(dock_button("Dock").empty(), "no Dock control before target selection");
		click("radar-contact/1");
		require(!dock_button("Dock").empty(), "selected radar contact exposes Dock in info panel");
		TakeScreenshot("build/context-dock.png");
		click(dock_button("Dock"));
		require(!dock_button("Cancel docking").empty(), "approach offers contextual cancellation");
		click(dock_button("Cancel docking"));
		click(dock_button("Dock"));
		ConsoleModel helm;
		for (int i = 0; i < 3000; ++i)
		{
			app.session->tick();
			if (i % 20 == 0)
				frame();
			helm.refresh(app.session);
			if (helm.docking()["ports"][0]["phase"] == "docked")
				break;
		}
		frame();
		require(helm.docking()["ports"][0]["phase"] == "docked" && !dock_button("Undock").empty(),
		        "real docking changes the info action to Undock");
		draw();
		draw();
		TakeScreenshot("build/context-undock.png");
		click(dock_button("Undock"));
		require(dock_button("Undock").empty() && dock_button("Dock").empty(), "separating offers no duplicate action");
		for (int i = 0; i < 5; ++i)
			frame();
		require(!dock_button("Dock").empty(), "finished separation restores contextual Dock");
		close_window();
		std::cout << "PASS radar contact info Dock, Cancel and Undock through native pointer input\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
