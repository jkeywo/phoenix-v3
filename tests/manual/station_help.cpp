#include <raylib.h>
#include <RmlUi/Core.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.session.local;
import phoenix.presentation;
import phoenix.ui.rml;
import phoenix.content.assets;
using namespace phoenix;
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc != 3)
			throw std::runtime_error("Supply base.zip and test-missions.zip");
		std::filesystem::create_directories("build/station-help-captures");
		open_window();
		opened = true;
		SetWindowSize(1280, 720);
		int popups = 0;
		for (const auto &hull : {std::string("base.rescue-courier"), std::string("base.cruiser")})
		{
			Application app;
			app.can_host = true;
			app.native_menu = false;
			app.session = make_local_session();
			app.session->add_pack(read_file(argv[1]));
			app.session->add_pack(read_file(argv[2]));
			app.session->start({"base.free-flight", {{"base.player", hull}}, "base.player", "helm"});
			const auto ship = app.session->observation()->own.id;
			app.screen = Screen::console;
			app.console_regions = {{1, 0, 0, 1280, 720}};
			auto frame = [&](const std::string &capture_path = std::string{})
			{
				BeginDrawing();
				ClearBackground(BLACK);
				draw_screen(app);
				if (!capture_path.empty())
				{
					auto capture = LoadImageFromScreen();
					ExportImage(capture, capture_path.c_str());
					UnloadImage(capture);
				}
				EndDrawing();
			};
			auto advance = [&]
			{
				for (int i = 0; i < 6; ++i)
				{
					app.session->tick();
					frame();
				}
			};
			auto act = [&](const std::string &id)
			{
				if (!app.screen_action(id))
					throw std::runtime_error("Help smoke action refused: " + id);
				advance();
			};
			auto present = [&](const std::string &id)
			{
				return std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
				                   [&](const auto &c)
				                   {
					                   return c.first == id;
				                   });
			};
			const auto seats = hull == "base.cruiser" ? std::vector<std::string>{"helm",    "tactical", "engineering",
			                                                                     "science", "comms",    "captain"}
			                                          : std::vector<std::string>{"helm", "support"};
			for (const auto &seat : seats)
			{
				app.session->release_console(1);
				if (!app.session->claim_console(1, {ship, seat}))
					throw std::runtime_error("Help smoke station claim refused: " + seat);
				const int levels = seat == "comms" || seat == "captain" ? 1 : 3;
				for (int level = 0; level < levels; ++level)
				{
					if (levels > 1 && !app.session->console_command(1, {ship, "preset", level}))
						throw std::runtime_error("Help smoke complexity refused");
					advance();
					for (int frame_index = 0; frame_index < 600 && !present("console/1/help"); ++frame_index)
						frame();
					std::vector<std::string> tabs;
					for (const auto &[id, label] : app.screen_controls)
						if (id.starts_with("console/1/console-page/"))
							tabs.push_back(id);
					if (tabs.empty())
						tabs.push_back("");
					for (const auto &tab : tabs)
					{
						if (!tab.empty())
							act(tab);
						if (!present("console/1/help"))
							throw std::runtime_error("Missing tab help: " + hull + "/" + seat + "/" + tab);
						act("console/1/help");
						auto *context = Rml::GetContext("console-1");
						if (!context || !context->GetFocusElement() ||
						    context->GetFocusElement()->GetId() != "console/1/systems-close")
							throw std::runtime_error("Help popup did not receive focus");
						auto *dialog = context->GetDocument(0)->GetElementById("fault-dialog");
						const auto at = dialog->GetAbsoluteOffset(Rml::BoxArea::Border);
						std::cout << "HELP-BOUNDS " << seat << " " << tab << " " << at.x << "," << at.y << " "
						          << dialog->GetOffsetWidth() << "x" << dialog->GetOffsetHeight() << '\n';
						if (at.x < 0 || at.y < 0 || at.x + dialog->GetOffsetWidth() > 1281 ||
						    at.y + dialog->GetOffsetHeight() > 721)
							throw std::runtime_error("Help popup extends beyond the Console viewport");
						context->ProcessKeyDown(Rml::Input::KI_TAB, 0);
						context->ProcessKeyUp(Rml::Input::KI_TAB, 0);
						if (context->GetFocusElement()->GetId() != "console/1/systems-close")
							throw std::runtime_error("Help popup did not contain keyboard focus");
						const auto help_text = std::count_if(app.screen_readouts.begin(), app.screen_readouts.end(),
						                                     [](const auto &r)
						                                     {
							                                     return r.first.starts_with("console/1/popup/help/");
						                                     });
						if (!help_text || !present("console/1/systems-close"))
							throw std::runtime_error("Help popup has no readable text or Close control");
						if (popups < 8 || (hull == "base.cruiser" && level == 0))
							frame("build/station-help-captures/help-" + std::to_string(popups) + ".png");
						++popups;
						if (popups % 2)
						{
							context->ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
							context->ProcessKeyUp(Rml::Input::KI_ESCAPE, 0);
							advance();
						}
						else
							act("console/1/systems-close");
						if (!present("console/1/help"))
							throw std::runtime_error("Help dismissal failed to restore the tab control");
						if (context->GetFocusElement()->GetId() != "console/1/help")
							throw std::runtime_error("Help dismissal did not restore focus");
					}
				}
			}
		}
		close_window();
		opened = false;
		std::cout << "PASS eight stations, supported complexities and " << popups << " tab help popups\n";
	}
	catch (const std::exception &e)
	{
		if (opened)
			close_window();
		std::cerr << e.what() << '\n';
		return 1;
	}
}
