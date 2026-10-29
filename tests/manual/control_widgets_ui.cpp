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
import phoenix.content.documents;
import phoenix.ui.preview;
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
		ConsoleModel labels;
		labels.refresh(app.session);
		const auto ordinary = labels.gameplay_controls();
		require(control->second.starts_with("Speed ") && std::any_of(ordinary.begin(), ordinary.end(),
		                                                             [&](const auto &row)
		                                                             {
			                                                             return row.second == control->second;
		                                                             }),
		        "widget and ordinary Console controls share numeric labels");
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
		require(has_readout("Maximum speed: 17000"),
		        "three-star override moves the same datum onto its main visual part");
		TakeScreenshot("build/control-widget-three-star.png");
		SetWindowSize(640, 720);
		frames();
		require(has_readout("Requested speed") && has_readout("Maximum speed: 17000"),
		        "compact layout reunites the widget without losing data");
		TakeScreenshot("build/control-widget-compact.png");
		WorkshopDocument draft;
		draft.load_pack(read_file(argv[1]));
		auto sample = draft.preview_ui()->consoles.at("test.console");
		sample["_preview_widget_context"] = "test.widget-ship/drive";
		for (int stars = 1; stars <= 3; ++stars)
		{
			sample["_preview_stars"] = stars;
			Texture2D texture{};
			for (int frame = 0; frame < 3; ++frame)
				texture = draw_console_preview(sample, 1200, 700, {-1, -1}, false, false, 0, false, draft.preview_ui());
			require(console_preview_geometry().placements.size() == 2,
			        "Workshop fits the selected ship's split widget parts through the real Console surface");
			auto image = LoadImageFromTexture(texture);
			ImageFlipVertical(&image);
			ExportImage(image, ("build/control-widget-workshop-" + std::to_string(stars) + ".png").c_str());
			UnloadImage(image);
			const auto preview = console_preview_state();
			require(preview.at("controllers").size() == 1 &&
			            preview.at("controllers")[0].at("widget") == "test.drive" &&
			            preview.at("controllers")[0].at("commands").at("speed").at("human") == (stars >= 2),
			        "Workshop renders the selected ship's widget and star responsibility through the real host");
			require(preview.at("controllers")[0].at("commands").at("speed").at("maximum") == 17000,
			        "preview controls respect the attached synthetic engine's speed limit");
			require(preview.at("controllers")[0].at("commands").at("speed").at("value") == 3000,
			        "Workshop begins with the selected widget's authored sample setting");
			require((stars == 1) == preview.at("controls").empty(),
			        "automated preview exposes inspection without command controls");
			if (stars == 3)
			{
				for (const bool pressed : {true, false})
					draw_console_preview(sample, 1200, 700, {950, 100}, pressed, !pressed, 0, false,
					                     draft.preview_ui());
				draw_console_preview(sample, 1200, 700, {-1, -1}, false, false, 0, false, draft.preview_ui());
				require(console_preview_state().at("controllers")[0].at("commands").at("speed").at("value") == 17000,
				        "Workshop pointer input changes the isolated widget sample through command admission");
			}
		}
		clear_console_preview();
		close_window();
		std::cout << "PASS shared widget readouts, details, command admission, compact reunion and Workshop ship/star "
		             "previews\n";
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
