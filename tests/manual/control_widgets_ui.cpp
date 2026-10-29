// Explicit graphical journey. Synthetic input is not physical-device evidence.
#include "../support/assertions.hpp"
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <set>
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
		const bool inspect = argc == 8 && std::string(argv[1]) == "--inspect";
		require(argc == 2 || inspect, "usage: phoenix_control_widgets_smoke synthetic-pack.zip | --inspect pack map "
		                              "slot ship console output-prefix");
		Application app;
		app.can_host = app.native_menu = true;
		app.session = make_local_session();
		app.session->add_pack(read_file(argv[inspect ? 2 : 1]));
		if (inspect)
			app.session->start({argv[3], {{argv[4], argv[5]}}, argv[4], argv[6]});
		else
			app.session->start({"test.map", {{"test.slot", "test.widget-ship"}}, "test.slot", "drive"});
		app.screen = Screen::console;
		open_window();
		opened = true;
		const auto frames = [&]
		{
			for (int frame = 0; frame < 80; ++frame)
			{
				if (inspect)
					app.session->tick();
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
		if (inspect)
		{
			const auto started = GetTime();
			while (!app.session->observation() && GetTime() - started < 90)
				frames();
			require(app.session->observation().has_value(), "production Console preparation timed out");
			frames();
			TakeScreenshot((std::string(argv[7]) + "-main.png").c_str());
			const auto controls = app.screen_controls;
			int page = 0;
			for (const auto &[action, label] : controls)
				if (action.find("station-page/") != std::string::npos && app.screen_action(action))
				{
					frames();
					TakeScreenshot((std::string(argv[7]) + "-page-" + std::to_string(++page) + ".png").c_str());
					std::cout << "Captured " << label << ": " << app.screen_controls.size() << " controls, "
					          << app.screen_readouts.size() << " readouts\n";
				}
			SetWindowSize(640, 720);
			frames();
			TakeScreenshot((std::string(argv[7]) + "-compact.png").c_str());
			close_window();
			return 0;
		}
		frames();
		const auto native_controls = [&]
		{
			return std::count_if(app.screen_controls.begin(), app.screen_controls.end(),
			                     [](const auto &row)
			                     {
				                     return row.first.starts_with("console/1/control-native/");
			                     });
		};
		require(native_controls() == 0, "automated workflow widget renders without native controls");
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
		require(native_controls() == 0, "human flight commands do not expose delegated native workflow controls");
		const auto range = "console/1/power/widget/control/propulsion/controls/trim";
		require(std::any_of(app.screen_inputs.begin(), app.screen_inputs.end(),
		                    [&](const auto &field)
		                    {
			                    return field.id == range;
		                    }),
		        "logical widget range reuses the existing Console slider input path");
		require(app.set_control_value(range, "70"), "shared widget range accepts an admitted setting");
		app.session->tick();
		frames();
		require(app.session->observation()->gameplay.at("controllers")[0].at("commands").at("trim").at("value") == 7,
		        "widget slider changes its actual Controller setting");
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
		require(!app.set_control_value(range, "20"), "modal details reject background widget range input");
		require(app.screen_action("console/1/systems-close"), "return from detail inspection");
		require(app.session->command({1, "preset", 2, {}}), "select three-star data placement");
		app.session->tick();
		frames();
		require(has_readout("Maximum speed: 17000"),
		        "three-star override moves the same datum onto its main visual part");
		require(native_controls() > 0, "native workflow renderer exposes only its allocated operation controls");
		const auto initial_quantity = std::find_if(app.screen_readouts.begin(), app.screen_readouts.end(),
		                                           [](const auto &row)
		                                           {
			                                           return row.second.starts_with("Transfer quantity:");
		                                           })
		                                  ->second;
		const auto native_action =
		    std::find_if(app.screen_controls.begin(), app.screen_controls.end(),
		                 [](const auto &row)
		                 {
			                 return row.first.starts_with("console/1/control-native/control/propulsion/") &&
			                        row.second == "Increase transfer quantity";
		                 })
		        ->first;
		require(app.screen_action(native_action),
		        "logical widget routes native renderer input through existing workflow UI");
		frames();
		std::set<std::string> quantities;
		for (const auto &[key, text] : app.screen_readouts)
			if (text.starts_with("Transfer quantity:"))
				quantities.insert(text);
		require(quantities.size() == 2 && quantities.contains(initial_quantity),
		        "separate widget instances retain independent native browsing state");
		const auto changed_quantity = *std::find_if(quantities.begin(), quantities.end(),
		                                            [&](const auto &text)
		                                            {
			                                            return text != initial_quantity;
		                                            });
		TakeScreenshot("build/control-widget-three-star.png");
		SetWindowSize(640, 720);
		frames();
		require(has_readout("Requested speed") && has_readout("Maximum speed: 17000"),
		        "compact layout reunites the widget without losing data");
		require(native_controls() > 0 && has_readout(changed_quantity) && has_readout(initial_quantity),
		        "compact reunion retains each widget's native part and browsing state");
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
			require(console_preview_geometry().placements.size() == 3,
			        "Workshop fits the selected ship's split widget parts through the real Console surface");
			auto image = LoadImageFromTexture(texture);
			ImageFlipVertical(&image);
			ExportImage(image, ("build/control-widget-workshop-" + std::to_string(stars) + ".png").c_str());
			UnloadImage(image);
			const auto preview = console_preview_state();
			require(preview.at("controllers").size() == 2 &&
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
				const auto controls = console_preview_controls();
				const auto maximum =
				    std::find_if(controls.begin(), controls.end(),
				                 [](const auto &control)
				                 {
					                 const auto label = control.at("label").template get<std::string>();
					                 return label.starts_with("Speed ") && label.find("17000") != std::string::npos;
				                 });
				require(maximum != controls.end(), "Workshop exposes the rendered maximum-speed control geometry");
				const Vector2 point{maximum->at("x").get<float>() + maximum->at("width").get<float>() / 2,
				                    maximum->at("y").get<float>() + maximum->at("height").get<float>() / 2};
				for (const bool pressed : {true, false})
					draw_console_preview(sample, 1200, 700, point, pressed, !pressed, 0, false, draft.preview_ui());
				draw_console_preview(sample, 1200, 700, {-1, -1}, false, false, 0, false, draft.preview_ui());
				const auto updated = console_preview_state().at("controllers")[0];
				require(updated.at("commands").at("speed").at("value") == 17000 &&
				            updated.at("generation") == preview.at("controllers")[0].at("generation"),
				        "Workshop pointer input updates sample state without changing command ownership");
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
