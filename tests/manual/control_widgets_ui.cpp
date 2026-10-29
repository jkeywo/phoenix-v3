// Explicit graphical journey. Synthetic input is not physical-device evidence.
#include "../support/assertions.hpp"
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.navigation;
import phoenix.presentation;
import phoenix.session.local;
import phoenix.console;
using namespace phoenix;
using phoenix::test::require;

int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		require(argc == 2, "usage: phoenix_control_widgets_smoke synthetic-pack.zip");
		Application app;
		app.can_host = app.native_menu = true;
		app.session = make_local_session();
		app.session->add_pack(read_file(argv[1]));
		app.session->start({"test.map", {{"test.slot", "test.widget-ship"}}, "test.slot", "drive"});
		app.screen = Screen::console;
		open_window();
		opened = true;
		const auto frames = [&]
		{
			for (int frame = 0; frame < 80; ++frame)
			{
				BeginDrawing();
				ClearBackground(BLACK);
				draw_screen(app);
				EndDrawing();
			}
		};
		const auto has_readout = [&](const std::string &label)
		{
			return std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
			                   [&](const auto &row)
			                   {
				                   return row.second.starts_with(label);
			                   });
		};
		frames();
		TakeScreenshot("build/control-widget-automated.png");
		require(has_readout("Requested speed") && !has_readout("Maximum speed: 17000"),
		        "automated named part presents only main data");
		require(app.screen_action("console/1/control-details/drive/propulsion"), "open automated widget details");
		frames();
		require(has_readout("Maximum speed: 17000") && has_readout("Requested speed"),
		        "detail popup includes detail data and main context");
		TakeScreenshot("build/control-widget-details.png");
		require(app.screen_action("console/1/systems-close"), "close widget details");
		frames();
		require(app.session->command({1, "preset", 1, {}}), "select two-star responsibility");
		app.session->tick();
		frames();
		const auto control = std::find_if(app.screen_controls.begin(), app.screen_controls.end(),
		                                  [](const auto &row)
		                                  {
			                                  return row.first.starts_with("console/1/task/control/");
		                                  });
		TakeScreenshot("build/control-widget-two-star.png");
		if (control == app.screen_controls.end())
		{
			std::cerr << app.session->observation()->gameplay.at("controllers").dump() << '\n';
			for (const auto &[id, label] : app.screen_controls)
				std::cerr << id << ": " << label << '\n';
		}
		require(control != app.screen_controls.end(), "interactive named part exposes admitted command controls");
		const auto action = control->first;
		require(app.screen_action(action), "widget control uses ordinary command admission");
		app.session->tick();
		frames();
		require(app.screen_action("console/1/control-details/drive/propulsion"), "inspect interactive widget details");
		frames();
		require(!app.screen_action(action), "modal details reject background command input");
		require(app.screen_action("console/1/systems-close"), "return from detail inspection");
		require(app.session->command({1, "preset", 2, {}}), "select three-star data placement");
		app.session->tick();
		frames();
		require(has_readout("Maximum speed: 17000"), "three-star override moves the same datum onto its main visual part");
		TakeScreenshot("build/control-widget-three-star.png");
		SetWindowSize(640, 720);
		frames();
		require(has_readout("Requested speed") && has_readout("Maximum speed: 17000"),
		        "compact layout reunites the widget without losing data");
		TakeScreenshot("build/control-widget-compact.png");
		close_window();
		std::cout
		    << "PASS shared widget readouts, detail popup, star mappings, command admission and compact reunion\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
