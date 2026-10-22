// Explicit graphical evidence using the shipped rescue Courier and semantic actions.
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <iostream>
#include <algorithm>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.session.execution;
using namespace phoenix;
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_sensors_smoke pack-directory");
		const bool long_range = argc > 2 && std::string(argv[2]) == "--long-range";
		Application app;
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));

		if (long_range)
		{
			const auto pack = read_pack(read_file(std::string(argv[1]) + "/base.zip"));
			const auto raw = parse_json(pack.files().at("definitions.json"), "sensor smoke definitions");
			Json ship;
			for (const auto &entity : raw.at("entities"))
				if (entity.at("id") == "base.rescue-courier")
					ship = entity;
			ship["systems"]["radar"]["range_mm"] = 30000;
			ship["systems"]["radar"]["long_range_mm"] = 1050000;
			PackDraft draft;
			draft.manifest = {{"schema", 1},
			                  {"id", "sensor-smoke"},
			                  {"parent", "phoenix-base"},
			                  {"overrides", {"base.rescue-courier"}}};
			draft.files["definitions.json"] = json_bytes(Json{{"schema", 4}, {"entities", Json::array({ship})}});
			app.session->add_pack(write_pack(draft));
		}
		app.session->start({"base.rescue-under-threat", {{"courier", "base.rescue-courier"}}, "courier", "helm"});
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(1280, 900);
		auto frame = [&]
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
		};
		for (int i = 0; i < 300; ++i)
			frame();
		auto advance = [&](int n)
		{
			while (n--)
			{
				app.session->tick();
				frame();
			}
		};
		auto act = [&](const std::string &id)
		{
			if (!app.screen_action("console/1/" + id))
				throw std::runtime_error("Refused sensor UI action: " + id);
			frame();
			frame();
		};
		auto button = [&](const std::string &label)
		{
			for (const auto &c : app.screen_controls)
				if (c.second.ends_with(label))
				{
					auto id = c.first;
					if (!app.screen_action(id))
						throw std::runtime_error("Refused " + label);
					frame();
					return;
				}
			throw std::runtime_error("Missing " + label);
		};
		auto capture = [&](const char *name)
		{
#ifdef __EMSCRIPTEN__
			// clang-format off
 EM_ASM({if(!Module.sensorCaptures)Module.sensorCaptures={};Module.sensorCaptures[UTF8ToString($0)]=Module.canvas.toDataURL("image/png");},name);
			// clang-format on
#else
			TakeScreenshot(name);
#endif
		};
		advance(5);
		act("game.console/tactical");
		act("game.sensor-contact/1");

		if (long_range)
		{
			bool found = false;
			for (const auto &control : app.screen_controls)
				found |= control.second.find("Range unknown") != std::string::npos;
			if (!found)
				throw std::runtime_error("Bearing-only semantic contact unavailable");
			capture("sensors-bearing.png");
		}
		button("Scan target");
		advance(70);
		capture("sensors-partial.png");
		button("Clear scan target");
		advance(8);
		capture("sensors-paused.png");
		button("Scan target");
		advance(450);
		capture("sensors-acquired.png");
		bool acquired = false;
		const auto observed = app.session->observation();
		for (const auto &v : observed->additional_consoles)
			if (v.console == "tactical")
				for (const auto &c : v.gameplay.at("sensors").at("contacts"))
					if (c.at("id") == 1)
						acquired = c.at("findings").contains("compartment_condition");
		if (!acquired)
			throw std::runtime_error("Courier did not acquire compartment condition");
		button("Clear scan target");
		advance(3);
		capture("sensors-retained.png");

		if (long_range)
		{
			const auto current = app.session->observation();
			auto view = *current;
			for (const auto &v : current->additional_consoles)
				if (v.console == "tactical")
					view = v;
			auto issue = [&](const std::string &controller, const std::string &channel, std::int64_t value)
			{
				for (const auto &c : view.gameplay.at("controllers"))
					if (c.at("id") == controller)
					{
						OperatingCommand cmd{view.own.id, {}, value, {}, controller, channel, c.at("generation")};
						cmd.console = "tactical";
						if (!app.session->command(cmd))
							throw std::runtime_error("Refused bearing launch command");
						return;
					}
				throw std::runtime_error("Missing bearing launch Controller: " + controller);
			};
			issue("scanner", "enabled", 0);
			advance(2);
			act("game.combat-target/1");
			issue("missile-launcher", "target", 1);
			advance(5);
			capture("sensors-bearing-launch.png");
			const auto projectiles =
			    parse_json(read_pack(app.session->save()).files().at("session.json"), "sensor smoke save")
			        .at("state")
			        .at("projectiles");
			if (std::none_of(projectiles.begin(), projectiles.end(),
			                 [&](const auto &p)
			                 {
				                 return p.at("source") == view.own.id && p.at("bearing_launch").template get<bool>();
			                 }))
				throw std::runtime_error("No bearing-only missile launched");
		}
		std::cout << "PASS Courier Tactical select, slew, pause, resume, acquire and retained findings\n";
		close_window();
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "FAIL " << e.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
