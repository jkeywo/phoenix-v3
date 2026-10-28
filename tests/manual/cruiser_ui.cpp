// Explicit native Console inspection, excluded from CTest and production tuning checks.
#include <raylib.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.navigation;
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
		const bool workloads = argc > 3 && std::string(argv[3]) == "--workloads";
		const bool remaining = argc > 3 && std::string(argv[3]) == "--remaining-stations";
		auto draft = read_pack(read_file(argv[1])).draft();
		auto definitions = parse_json(draft.files.at("definitions.json"), "Cruiser clinical UI variant");
		for (auto &entity : definitions["entities"])
			if (entity["id"] == "base.cruiser")
			{
				if (!workloads)
					entity["personnel"]["officers"][1]["condition"] = "minor";
				else
					for (auto &store : entity["logistics"]["stores"])
						if (store["id"] == "main-hold")
						{
							store["initial"]["parts"] = 0;
							store["initial"]["medical"] = 0;
						}
			}
		draft.files["definitions.json"] = json_bytes(definitions);
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(write_pack(draft));
		if (argc > 2)
		{
			auto child = read_pack(read_file(argv[2])).draft();
			if (workloads)
			{
				auto data = parse_json(child.files.at("definitions.json"), "Quiet Cruiser UI variant");
				for (auto &map : data["maps"])
					if (map["id"] == "base.cruiser-combat")
						map.erase("scenario");
				child.files["definitions.json"] = json_bytes(data);
			}
			if (remaining)
			{
				auto data = parse_json(child.files.at("definitions.json"), "Station Objectives UI scene");
				data["trees"].push_back(
				    Json::parse(R"json({"id":"base.smoke.station-objectives","context":"scenario","initial":"show",
 "states":[{"id":"show","entry":[{"action":"add_objective","arguments":["destination",{"observation":"self.entity"},"destination"]}]}]})json"));
				for (auto &map : data["maps"])
					if (map["id"] == "base.free-flight")
					{
						map["regions"] = Json::array({{{"id", "destination"},
						                               {"shape", "circle"},
						                               {"x_mm", 3000000},
						                               {"y_mm", 3000000},
						                               {"radius_mm", 10000}}});
						map["scenario"] = {
						    {"tree", "base.smoke.station-objectives"},
						    {"bindings", {{"actor", {{"slot", "base.player"}}}}},
						    {"objectives",
						     Json::array({{{"id", "destination"},
						                   {"type", "reach_region"},
						                   {"region", "destination"},
						                   {"actor", {{"slot", "base.player"}}},
						                   {"recipients", {{"ship", {{"slot", "base.player"}}}}},
						                   {"title_key", "Reach the rendezvous"},
						                   {"description_key", "Bring the ship to the marked destination."}}})}};
					}
				child.files["definitions.json"] = json_bytes(data);
			}
			app.session->add_pack(write_pack(child));
		}
		const auto slot = workloads ? "cruiser" : "base.player";
		app.session->start({workloads ? "base.cruiser-combat" : "base.free-flight",
		                    {{slot, "base.cruiser"}},
		                    slot,
		                    workloads ? "helm" : "science"});
		const auto ship = app.session->observation()->own.id;
		if (!app.session->claim_console(2, {ship, "tactical"}))
			throw std::runtime_error("Cruiser Tactical claim refused");
		if (workloads && !app.session->claim_console(3, {ship, "engineering"}))
			throw std::runtime_error("Cruiser Engineering claim refused");
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(workloads || remaining ? 1920 : 2560, workloads || remaining ? 1080 : 900);
		if (workloads || remaining)
			app.console_regions = {{1, 0, 0, 1920, 1080}};
		else
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
			if (workloads)
				ready = std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
				                    [](const auto &c)
				                    {
					                    return c.first.starts_with("console/1/");
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
		if (remaining)
		{
			if (!app.session->claim_console(3, {ship, "captain"}) || !app.session->claim_console(4, {ship, "comms"}))
				throw std::runtime_error("Remaining Cruiser station claim refused");
			for (int level = 0; level < 3; ++level)
			{
				if (!app.session->console_command(1, {ship, "preset", level}))
					throw std::runtime_error("Science workload refused");
				advance(4);
				const auto file = "build/station-work/science-" + std::to_string(level + 1) + ".png";
				frame(file.c_str());
			}
			for (std::uint64_t route : {3, 4})
			{
				app.console_regions = {{route, 0, 0, 1920, 1080}};
				advance(3);
				if (route == 3)
				{
					const auto observed = app.session->console_observation(route);
					bool selected = false;
					for (const auto &c : observed->gameplay.at("controls"))
						if (c.at("action") == "objective.priority" && c.at("value") != 0)
						{
							OperatingCommand choose{ship, "objective.priority", c.at("value")};
							choose.generation = c.at("generation");
							selected = app.session->console_command(route, choose);
							break;
						}
					if (!selected)
						throw std::runtime_error("Captain priority control absent");
					advance(3);
				}
				frame(route == 3 ? "build/station-work/captain.png" : "build/station-work/comms.png");
			}
			close_window();
			return 0;
		}
		if (workloads)
		{
			for (int level = 0; level < 3; ++level)
			{
				for (std::uint64_t route : {1, 2, 3})
					if (!app.session->console_command(route, {ship, "preset", level, {}}))
						throw std::runtime_error("Workload selection refused");
				for (std::uint64_t route : {1, 2, 3})
				{
					app.console_regions = {{route, 0, 0, 1920, 1080}};
					advance(3);
					const auto file =
					    "build/cruiser-workloads-" + std::to_string(route) + "-" + std::to_string(level + 1) + ".png";
					frame(file.c_str());
				}
			}
			if (!app.session->claim_console(4, {ship, "comms"}))
				throw std::runtime_error("Cruiser Comms UI claim refused");
			for (std::uint64_t route : {1, 2, 3})
				if (!app.session->console_command(route, {ship, "preset", 0, {}}))
					throw std::runtime_error("Assisted UI workload refused");
			auto show = [&](std::uint64_t route)
			{
				app.console_regions = {{route, 0, 0, 1920, 1080}};
				advance(1);
			};
			show(4);
			advance(80);
			action("console/4/comms/hail/ship:90/quartermaster");
			for (int i = 0; i < 100; ++i)
			{
				advance(2);
				if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
				                [](const auto &c)
				                {
					                return c.first == "console/4/comms/answer/both";
				                }))
					break;
			}
			action("console/4/comms/answer/both");
			advance(20);
			frame("build/cruiser-supply-comms-ui.png");
			auto mission = [&](std::uint64_t route)
			{
				const auto view = *app.session->console_observation(route);
				for (const auto &row : view.gameplay.at("away_missions"))
					if (row["id"] == "depot-gift")
						return row;
				throw std::runtime_error("UI conversation did not reveal the Engineering offer");
			};
			show(3);
			action("console/3/away");
			advance(1);
			action("console/3/away/open/depot-gift/");
			advance(1);
			show(2);
			action("console/2/away");
			advance(1);
			action("console/2/away/contribute/depot-gift/1");
			advance(1);
			if (mission(2).at("dispatch") != false || mission(2).at("contributing") != true)
				throw std::runtime_error("Tactical contribution UI violated Dispatch ownership");
			frame("build/cruiser-supply-escort-ui.png");
			show(3);
			action("console/3/away");
			advance(1);
			for (int i = 0; i < 100 && mission(3).at("blocker") != ""; ++i)
				advance(5);
			frame("build/cruiser-supply-prepared-ui.png");
			std::ofstream("build/cruiser-ui-prepared-receipt.json")
			    << Json{{"mission", mission(3)}, {"controls", app.screen_controls}}.dump(2);
			action("console/3/away/send/depot-gift/");
			advance(1);
			if (mission(3).at("phase") != "outbound")
				throw std::runtime_error("Engineering Dispatch UI did not launch the prepared trip");
			frame("build/cruiser-supply-dispatch-ui.png");
			std::ofstream("build/cruiser-ui-dispatch-receipt.json")
			    << Json{{"engineering", mission(3)}, {"tactical", mission(2)}}.dump(2);
			close_window();
			opened = false;
			std::cout << "PASS Cruiser Helm, Tactical and Engineering one/two/three-star console render and admitted "
			             "profile changes; Comms offer, Engineering dispatch and Tactical contribution through Console "
			             "controls\n";
			return 0;
		}
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
