// Explicit graphical journey. Synthetic input is not physical-device evidence.
#include "../support/assertions.hpp"
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.navigation;
import phoenix.presentation;
import phoenix.session.local;
import phoenix.console;
import phoenix.console_controls;
import phoenix.content.documents;
import phoenix.ui.preview;
using namespace phoenix;
using phoenix::test::require;
#include "courier_refinement.hpp"
#include "radar_coverage.hpp"

int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		const bool courier = argc == 8 && std::string(argv[1]) == "--courier";
		const bool refinement = argc == 8 && std::string(argv[1]) == "--refinement";
		const bool radar_coverage = argc == 8 && std::string(argv[1]) == "--radar-coverage";
		const bool inspect =
		    argc == 8 && (std::string(argv[1]) == "--inspect" || courier || refinement || radar_coverage);
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
			for (int frame = 0; frame < (courier ? 12 : 80); ++frame)
			{
				if (inspect && !app.session->observation())
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
			while ((!app.session->observation() || !app.console_surface_active || app.screen_controls.empty()) &&
			       GetTime() - started < 90)
				frames();
			require(app.session->observation().has_value(), "production Console preparation timed out");
			require(app.console_surface_active && !app.screen_controls.empty(),
			        "production Console has no rendered controls");
			if (radar_coverage)
			{
				courier_radar_coverage_smoke(app, argv[7], frames);
				close_window();
				return 0;
			}
			if (refinement)
			{
				courier_refinement_smoke(app, argv[7], frames);
				close_window();
				return 0;
			}
			if (courier)
			{
				const bool helm = std::string(argv[6]) == "helm";
				const bool nav_comms = std::string(argv[6]) == "nav-comms";
				for (int stars = 1; stars <= 3; ++stars)
				{
					std::cout << "Begin Courier star " << stars << std::endl;
					SetWindowSize(1920, 1080);
					require(app.session->command({app.session->observation()->own.id, "preset", stars - 1, {}}),
					        "set Courier responsibility");
					std::cout << "Preset admitted" << std::endl;
					for (int tick = 0; tick < 60; ++tick)
						app.session->tick();
					std::cout << "Ticks complete" << std::endl;
					frames();
					std::vector<std::pair<std::string, std::string>> tabs;
					const auto expected_tabs = static_cast<std::size_t>(nav_comms ? 2 : (helm ? 4 : 5) + (stars == 3));
					const auto preparing = GetTime();
					do
					{
						tabs.clear();
						for (const auto &row : app.screen_controls)
							if (row.first.find("station-page/") != std::string::npos ||
							    row.first.find("console-page/") != std::string::npos)
								tabs.push_back(row);
						if (tabs.size() == expected_tabs)
							break;
						frames();
					} while (GetTime() - preparing < 30);
					for (const auto &[key, label] : tabs)
						std::cout << "Prepared tab " << key << ": " << label << std::endl;
					require(tabs.size() == static_cast<std::size_t>(nav_comms ? 2 : (helm ? 4 : 5) + (stars == 3)),
					        "Courier has exactly its agreed station pages");
					const auto prefix = std::string(argv[7]) + "-" + std::to_string(stars);
					for (std::size_t page = 0; page < tabs.size(); ++page)
					{
						require(app.screen_action(tabs[page].first), "open Courier page");
						frames();
						std::cout << "Courier " << stars << " " << tabs[page].second << '\n';
						for (const auto &[action, label] : app.screen_controls)
							std::cout << "Visible " << action << ": " << label << '\n';
						for (const auto &[key, value] : app.screen_readouts)
							std::cout << "Readout " << value << '\n';
						TakeScreenshot((prefix + "-page-" + std::to_string(page) + ".png").c_str());
						const auto action_label = [&](const std::string &label)
						{
							const auto controls = app.screen_controls;
							for (const auto &[id, text] : controls)
								if (text == label)
									return app.screen_action(id);
							return false;
						};
						if (tabs[page].second == "Navigation")
						{
							require(probe_console_navigation(1, 0) && probe_console_navigation(1, 1),
							        "Navigation map supports pointer and keyboard selection");
							frames();
							require(action_label("Set waypoint"), "details panel sets the map selection as waypoint");
							app.session->tick();
							frames();
							require(action_label("Clear waypoint"), "details panel clears the shared waypoint");
							app.session->tick();
							frames();
							require(probe_console_navigation(1, 2), "FTL overlay does not select or pan the map");
							require(std::count_if(app.screen_controls.begin(), app.screen_controls.end(),
							                      [](const auto &row)
							                      {
								                      return row.second == "Prepare FTL";
							                      }) == 1,
							        "Navigation renders a single FTL panel");
							require(action_label("Prepare FTL"), "existing FTL preparation is available on Navigation");
							app.session->tick();
							frames();
							require(action_label("Cancel FTL"), "existing FTL preparation can be cancelled");
							app.session->tick();
							frames();
						}
						if (tabs[page].second == "Overview")
						{
							require(app.screen_action("console/1/camera/camera-mode/1") &&
							            app.screen_action("console/1/camera/camera-direction/2"),
							        "Support camera controls remain operable");
							require(std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
							                    [](const auto &row)
							                    {
								                    return row.first.find("/camera/camera-direction/") !=
								                           std::string::npos;
							                    }),
							        "Support reuses the illustrated camera direction widget");
							app.session->tick();
							frames();
							require(probe_console_support_visuals(1), "Condition slider numbers remain visible");
						}
						if (helm && page == 0)
						{
							require(std::any_of(app.screen_inputs.begin(), app.screen_inputs.end(),
							                    [](const auto &field)
							                    {
								                    return field.id.find("speed/") != std::string::npos;
							                    }),
							        "Helm retains the throttle slider");
							const auto observed = app.session->observation();
							if (!observed->contacts.empty())
							{
								require(app.screen_action("console/1/radar-contact/" +
								                          std::to_string(observed->contacts.front().id)),
								        "select a Courier radar contact");
								frames();
								const auto choices = app.screen_controls;
								for (const auto *label : {"Follow", "Match speed"})
								{
									const auto choice = std::find_if(choices.begin(), choices.end(),
									                                 [&](const auto &row)
									                                 {
										                                 return row.second == label;
									                                 });
									require(choice != choices.end() && app.screen_action(choice->first),
									        "selected-target navigation uses owned native channels");
									app.session->tick();
									frames();
								}
								TakeScreenshot((prefix + "-selected-target.png").c_str());
							}
							auto lateral = [&]
							{
								const auto controls = app.screen_controls;
								const auto button = std::find_if(controls.begin(), controls.end(),
								                                 [](const auto &row)
								                                 {
									                                 return row.second == "Left thrust";
								                                 });
								require(button != controls.end() && app.screen_action(button->first),
								        "hold the lateral control");
								app.session->tick();
								frames();
								bool held = false;
								const auto held_view = app.session->observation();
								for (const auto &owner : held_view->gameplay.at("controllers"))
									for (const auto &[key, channel] : owner.at("commands").items())
										held |=
										    channel.at("action") == "lateral_thrust" && channel.at("value") == -1000;
								require(held, "lateral button changes its actual admitted setting");
								require(app.screen_action(button->first), "release the lateral control");
								app.session->tick();
								frames();
							};
							if (stars >= 2)
								lateral();
							if (stars >= 2)
							{
								std::string loaded_before;
								for (const auto &[key, value] : app.screen_readouts)
									if (value.starts_with("Loaded:"))
										loaded_before = value;
								const auto controls = app.screen_controls;
								const auto type = std::find_if(controls.begin(), controls.end(),
								                               [](const auto &row)
								                               {
									                               return row.second == "Homing mag";
								                               });
								require(type != controls.end() && app.screen_action(type->first),
								        "choose a named missile type without an Auto option");
								app.session->tick();
								frames();
								const auto observed = app.session->observation();
								for (const auto &extra : observed->additional_consoles)
									if (extra.console == "tactical")
										for (const auto &weapon : extra.gameplay.at("combat").at("weapons"))
											if (weapon.at("unloading") > 0 && !loaded_before.empty())
												require(has_readout(loaded_before),
												        "changing selected type still displays the missile physically "
												        "loaded in the tube");
								TakeScreenshot((prefix + "-ammo-transition.png").c_str());
							}

							const auto controls = app.screen_controls;
							const auto fire = std::find_if(controls.begin(), controls.end(),
							                               [](const auto &row)
							                               {
								                               return row.second == "Fire";
							                               });
							require(fire != controls.end() && app.screen_action(fire->first),
							        "Courier Fire uses the full native request envelope");
							app.session->tick();
							frames();
							if (stars >= 2)
							{
								require(app.screen_action("console/1/manual"),
								        "Helm switches to manual flight at two and three stars");
								app.session->tick();
								frames();
								TakeScreenshot((prefix + "-manual.png").c_str());
								lateral();
								require(app.screen_action("console/1/manual"), "Helm returns to assisted flight");
								app.session->tick();
								frames();
							}
						}
						if (tabs[page].second == "Away Missions" &&
						    !app.session->observation()->gameplay.at("away_missions").empty())
						{
							const auto mission = app.session->observation()->gameplay.at("away_missions")[0];
							const auto controls = app.screen_controls;
							const auto item = std::find_if(controls.begin(), controls.end(),
							                               [&](const auto &row)
							                               {
								                               return row.second.starts_with(
								                                   mission.at("title").template get<std::string>());
							                               });
							require(item != controls.end() && app.screen_action(item->first),
							        "select a mission from the Courier list");
							frames();
							const auto choices = app.screen_controls;
							const auto activate = std::find_if(choices.begin(), choices.end(),
							                                   [](const auto &row)
							                                   {
								                                   return row.second == "Activate";
							                                   });
							if (activate != choices.end())
								require(app.screen_action(activate->first),
								        "activate mission preparation through ordinary admission");
							for (int tick = 0; tick < 90; ++tick)
								app.session->tick();
							frames();
							TakeScreenshot((prefix + "-mission.png").c_str());
							require(!app.session->observation()
							             ->gameplay.at("away_missions")[0]
							             .at("phase")
							             .template get<std::string>()
							             .starts_with("outbound"),
							        "occupied Courier does not automatically launch a prepared mission");
						}
						if (tabs[page].second == "Repair")
						{
							const auto controls = app.screen_controls;
							const auto available_teams = app.session->observation()->gameplay.at("teams");
							const auto team =
							    std::find_if(controls.begin(), controls.end(),
							                 [&](const auto &row)
							                 {
								                 const std::string prefix = "console/1/task/specialised/repair/";
								                 if (!row.first.starts_with(prefix))
									                 return false;
								                 const auto index = std::stoul(row.first.substr(prefix.size()));
								                 return index < available_teams.size() &&
								                        !available_teams[index].value("reserved", false);
							                 });
							require(team != controls.end() && app.screen_action(team->first),
							        "Repair roster selects a team");
							frames();
							TakeScreenshot((prefix + "-team-map.png").c_str());
							const auto map_controls = app.screen_controls;
							const auto teams = app.session->observation()->gameplay.at("teams");
							const auto room =
							    std::find_if(map_controls.begin(), map_controls.end(),
							                 [&](const auto &row)
							                 {
								                 const std::string prefix = "console/1/task/specialised/repair/";
								                 return row.first.starts_with(prefix) &&
								                        std::stoul(row.first.substr(prefix.size())) >= teams.size() &&
								                        row.second.find("Team here") == std::string::npos;
							                 });
							require(room != map_controls.end() && app.screen_action(room->first),
							        "compartment map submits an ordinary team assignment");
							app.session->tick();
							frames();
							const auto travelling = app.session->observation()->gameplay.at("teams");
							if (std::any_of(travelling.begin(), travelling.end(),
							                [](const auto &team)
							                {
								                return team.at("travel") > 0;
							                }))
								require(has_readout("Travelling"),
								        "travelling team view suppresses local compartment details");
							TakeScreenshot((prefix + "-team-travel.png").c_str());
						}
					}
					require(app.screen_action(tabs.front().first), "return to primary Courier view");
					frames();
					const auto message_control = std::find_if(app.screen_controls.begin(), app.screen_controls.end(),
					                                          [](const auto &row)
					                                          {
						                                          return row.first.ends_with("/messages");
					                                          });
					require(message_control != app.screen_controls.end(),
					        "Console publishes its message-history toggle");
					const auto message_action = message_control->first;
					require(app.screen_action(message_action), "open Console message history");
					frames();
					TakeScreenshot((prefix + "-messages.png").c_str());
					require(app.screen_action(message_action), "close Console message history");
					SetWindowSize(640, 720);
					frames();
					if (nav_comms)
						require(probe_console_navigation(1, 0), "compact Navigation retains a usable map");
					TakeScreenshot((prefix + "-compact.png").c_str());
					SetWindowSize(1920, 1080);
					frames();
					require(app.screen_action("console/1/settings"), "open local text settings");
					frames();
					for (int step = 0; step < 3; ++step)
					{
						require(app.screen_action("console/1/local-settings.larger"), "increase Console text size");
						frames();
					}
					require(app.screen_action("console/1/local-settings.close"), "close text settings");
					frames();
					TakeScreenshot((prefix + "-large-text.png").c_str());
					require(app.screen_action("console/1/settings"), "restore local text settings");
					frames();
					for (int step = 0; step < 3; ++step)
					{
						require(app.screen_action("console/1/local-settings.smaller"), "restore Console text size");
						frames();
					}
					require(app.screen_action("console/1/local-settings.close"), "return from local text settings");
					frames();
				}
				close_window();
				return 0;
			}
			for (const auto &[action, label] : app.screen_controls)
				std::cout << "Visible " << action << ": " << label << '\n';
			frames();
			TakeScreenshot((std::string(argv[7]) + "-main.png").c_str());
			const auto controls = app.screen_controls;
			int page = 0;
			for (const auto &[action, label] : controls)
				if ((action.find("station-page/") != std::string::npos ||
				     action.find("console-page/") != std::string::npos) &&
				    app.screen_action(action))
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
		const auto controls = app.screen_controls;
		const auto control = std::find_if(controls.begin(), controls.end(),
		                                  [](const auto &row)
		                                  {
			                                  return row.first.starts_with("console/1/task/control/") &&
			                                         row.second.starts_with("Speed ");
		                                  });
		const auto permitted = app.session->observation();
		for (const auto &descriptor : permitted->gameplay.at("controls"))
			if (descriptor.value("channel", std::string{}) == "enabled")
			{
				const auto label = console_control_label(descriptor);
				const bool rendered =
				    std::any_of(controls.begin(), controls.end(),
				                [&](const auto &row)
				                {
					                return row.first.starts_with("console/1/task/control/propulsion/controls/") &&
					                       row.second == label;
				                });
				require(rendered == (descriptor.at("value") == 1),
				        "Boolean part filter renders its matching numeric command and excludes the opposite control");
			}
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
		if (control == controls.end())
		{
			std::cerr << app.session->observation()->gameplay.at("controllers").dump() << '\n';
			for (const auto &[id, label] : app.screen_controls)
				std::cerr << id << ": " << label << '\n';
		}
		require(control != controls.end(), "interactive named part exposes admitted command controls");
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
