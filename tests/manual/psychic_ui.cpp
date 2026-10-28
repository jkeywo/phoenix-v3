// Explicit graphical journey; uses a private two-Cruiser free-flight variant.
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.navigation;
import phoenix.session.execution;
using namespace phoenix;
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_psychic_smoke pack.zip");
		auto draft = read_pack(read_file(argv[1])).draft();
		auto definitions = parse_json(draft.files.at("definitions.json"), "psychic UI variant");
		for (auto &map : definitions["maps"])
			if (map.at("id") == "base.free-flight")
				map["entities"].push_back(
				    {{"id", 1}, {"definition", "base.cruiser"}, {"x_mm", 1200000}, {"y_mm", 0}, {"heading_mdeg", 0}});
		draft.files["definitions.json"] = json_bytes(definitions);
		Application app;
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(write_pack(draft));
		app.session->start({"base.free-flight", {{"base.player", "base.cruiser"}}, "base.player", "science"});
		if (!app.session->claim_console(2, {1, "tactical"}) || !app.session->claim_console(3, {1, "science"}))
			throw std::runtime_error("Psychic probe claims refused");
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(2560, 900);
		app.console_regions = {{1, 0, 0, 1280, 900}, {2, 1280, 0, 1280, 900}};
		const auto frame = [&](const char *capture = nullptr)
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			if (capture)
			{
				auto image = LoadImageFromScreen();
				ExportImage(image, capture);
				UnloadImage(image);
			}
			EndDrawing();
		};
		for (int i = 0; i < 300; ++i)
			frame();
		const auto advance = [&](int count)
		{
			for (int i = 0; i < count; ++i)
			{
				app.session->tick();
				if (i % 10 == 0 || i == count - 1)
					frame();
			}
		};
		const auto command = [&](std::uint64_t route, const std::string &channel, std::int64_t value)
		{
			const auto o = app.session->console_observation(route);
			for (const auto &c : o->gameplay.at("controllers"))
				if (c.at("human") == true && c.at("commands").contains(channel))
				{
					if (!app.session->console_command(
					        route, {o->own.id, {}, value, {}, c.at("id"), channel, c.at("generation")}))
						throw std::runtime_error("Psychic intent refused");
					return;
				}
			throw std::runtime_error("Missing human ability Controller");
		};
		const auto press = [&](const std::string &label)
		{
			for (const auto &[id, text] : app.screen_controls)
				if (id.starts_with("console/1/") && text == label)
				{
					const auto action = id;
					if (!app.screen_action(action))
						throw std::runtime_error("Psychic UI action refused");
					return;
				}
			throw std::runtime_error("Missing psychic UI control: " + label);
		};
		advance(5);
		press("Phantom choir: Activate ability");
		advance(5);
		command(1, "phantoms-target", 1);
		command(1, "phantoms-enabled", 1);
		advance(8);
		const auto victim = app.session->console_observation(2);
		const auto count = std::count_if(victim->contacts.begin(), victim->contacts.end(),
		                                 [](const auto &c)
		                                 {
			                                 return c.id > 2 && c.id <= 1000000000ULL;
		                                 });
		if (count != 4)
			throw std::runtime_error("Cruiser projection did not produce its authored ghost group");
		frame("psychic-choir-ghosts.png");
		command(3, "ward-target", 1);
		command(3, "ward-enabled", 1);
		advance(8);
		const auto protected_view = app.session->console_observation(2);
		if (std::any_of(protected_view->contacts.begin(), protected_view->contacts.end(),
		                [](const auto &c)
		                {
			                return c.id > 2 && c.id <= 1000000000ULL;
		                }))
			throw std::runtime_error("Cruiser ward failed to suppress ghosts");
		frame("psychic-choir-ward.png");
		std::cout << "Shared ability Console ghost group and ward journey passed\n";
		close_window();
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
