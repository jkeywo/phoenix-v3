// Explicit native Console inspection, excluded from CTest and production tuning checks.
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.console;
import phoenix.session.execution;
using namespace phoenix;
int main(int argc, char **argv)
{
	Application app;
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_cruiser_ui_smoke pack.zip");
		auto draft = read_pack(read_file(argv[1])).draft();
		auto definitions = parse_json(draft.files.at("definitions.json"), "Cruiser clinical UI variant");
		for (auto &entity : definitions["entities"])
			if (entity["id"] == "base.cruiser")
				entity["personnel"]["officers"][1]["condition"] = "minor";
		draft.files["definitions.json"] = json_bytes(definitions);
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(write_pack(draft));
		app.session->start({"base.free-flight", {{"base.player", "base.cruiser"}}, "base.player", "science"});
		const auto ship = app.session->observation()->own.id;
		if (!app.session->claim_console(2, {ship, "tactical"}))
			throw std::runtime_error("Cruiser Tactical claim refused");
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(2560, 900);
		app.console_regions = {{1, 0, 0, 1280, 900}, {2, 1280, 0, 1280, 900}};
		auto frame = [&](const char *capture = nullptr)
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
		bool ready = false;
		for (int i = 0; i < 600 && !ready; ++i)
		{
			frame();
			ready = std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                    [](const auto &c)
			                    {
				                    return c.first == "console/1/game.teams";
			                    });
		}
		if (!ready)
			throw std::runtime_error("Cruiser Console graphics did not become ready");
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
			if (std::none_of(app.screen_controls.begin(), app.screen_controls.end(),
			                 [&](const auto &c)
			                 {
				                 return c.first == id;
			                 }))
				throw std::runtime_error("Cruiser UI control missing: " + id);
			auto copy = id;
			if (!app.screen_action(copy))
				throw std::runtime_error("Cruiser UI action refused: " + id);
		};
		advance(2);
		action("console/1/game.teams");
		action("console/2/personnel");
		advance(1);
		action("console/1/game.team-order/medical-1/treat");
		advance(1);
		frame("cruiser-departments-ui.png");
		ConsoleModel clinical;
		clinical.refresh(app.session, 1);
		bool selected = false;
		for (const auto &room : clinical.team_map())
			if (room["id"] == "engines")
			{
				action("console/1/game.compartment/" + std::to_string(room.at("index").get<int>()));
				selected = true;
				break;
			}
		if (!selected)
			throw std::runtime_error("Cruiser clinical map has no engine compartment");
		advance(700);
		frame("cruiser-treatment-ui.png");
		const auto observation = app.session->console_observation(1);
		for (const auto &team : observation->gameplay.at("teams"))
			for (const auto &patient : team.at("patients"))
				if (patient.at("id") == "officer-2")
					throw std::runtime_error("Cruiser UI treatment did not heal the patient");
		close_window();
		opened = false;
		std::cout << "PASS Cruiser Science Medical and Tactical Security controls and clinical map order\n";
	}
	catch (const std::exception &e)
	{
		if (opened)
			close_window();
		std::cerr << e.what() << '\n';
		return 1;
	}
}
