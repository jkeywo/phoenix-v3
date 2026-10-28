// Explicit graphical smoke. Uses the real authored duel and ordinary player commands.
#include <raylib.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <atomic>
#include <cmath>
#include <functional>
#include <mutex>
#include <set>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.graphics.scene;
import phoenix.navigation;
import phoenix.session.execution;
using namespace phoenix;
#include "host_audio_smoke.hpp"
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_combat_smoke pack-directory");
		const bool mvp = argc > 2 && std::string(argv[2]) == "mvp";
		const bool support = argc > 2 && std::string(argv[2]) == "support";
		const bool destruction = argc > 2 && std::string(argv[2]) == "destruction";
		const bool audio = argc > 2 && std::string(argv[2]) == "audio";
		Application app;
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		app.session->add_pack(read_file(std::string(argv[1]) + "/base.zip"));
		if (FileExists((std::string(argv[1]) + "/test-missions.zip").c_str()))
			app.session->add_pack(read_file(std::string(argv[1]) + "/test-missions.zip"));
		if (destruction)
		{
			auto enemy = app.session->catalog().entities.at("base.pirate-raider");
			for (auto &room : enemy["combat"]["decks"][0]["compartments"])
				room["initial_hp"] = 0;
			enemy["combat"]["decks"][0]["compartments"][4]["initial_hp"] = 1;
			for (auto &system : enemy["systems"].items())
				if (system.value().value("kind", std::string{}) == "repair_team")
					system.value()["enabled"] = false;
			auto map = app.session->catalog().maps.at("base.asteroid-duel");
			map["regions"] = Json::array();
			map["entities"][0]["x_mm"] = 80000;
			map["entities"][0]["y_mm"] = 0;
			map["ship_slots"][0]["x_mm"] = 0;
			map["ship_slots"][0]["y_mm"] = 0;
			PackDraft draft;
			draft.manifest = {{"schema", 1},
			                  {"id", "combat-smoke"},
			                  {"parent", "phoenix-base"},
			                  {"overrides", {"base.pirate-raider", "base.asteroid-duel"}}};
			draft.files["definitions.json"] =
			    json_bytes(Json{{"schema", 4}, {"entities", Json::array({enemy})}, {"maps", Json::array({map})}});
			app.session->add_pack(write_pack(draft));
		}

		const auto &duel_slot = app.session->catalog().maps.at("base.asteroid-duel").at("ship_slots")[0];
		const auto duel_ship = duel_slot.value("default_ship", duel_slot.at("allowed_ships")[0].get<std::string>());
		GameSetup setup{
		    "base.asteroid-duel", {{"base.duellist", duel_ship}}, "base.duellist", support ? "support" : "helm"};
		if (audio)
		{
			setup.console_profiles["base.duellist"] = {{"helm", "detailed"}, {"support", "detailed"}};
			auto map = app.session->catalog().maps.at("base.asteroid-duel");
			map["regions"] = Json::array();
			map["entities"][0]["x_mm"] = 60000;
			map["entities"][0]["y_mm"] = 0;
			map["ship_slots"][0]["x_mm"] = 0;
			map["ship_slots"][0]["y_mm"] = 0;
			PackDraft draft;
			draft.manifest = {{"schema", 1},
			                  {"id", "audio-smoke"},
			                  {"parent", "phoenix-base"},
			                  {"overrides", {"base.asteroid-duel"}}};
			draft.files["definitions.json"] = json_bytes(Json{{"schema", 4}, {"maps", Json::array({map})}});
			app.session->add_pack(write_pack(draft));
		}
		app.session->start(setup);
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(1280, 900);
		const auto frame = [&]()
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
		};
		frame();
		for (int loading = 0; loading < 400; ++loading)
		{
			frame();
			if (std::none_of(app.screen_controls.begin(), app.screen_controls.end(),
			                 [](const auto &c)
			                 {
				                 return c.first == "loading.cancel";
			                 }))
				break;
		}
		frame();
		if (audio)
		{
			run_host_audio_smoke(app, frame);
			close_window();
			return 0;
		}
		if (support)
		{
			if (!app.screen_action("console/1/game.compartment/1"))
				throw std::runtime_error("Compartment map action unavailable");
			frame();
			frame();
			bool assigned = false;
			for (const auto &control : app.screen_controls)
				if (control.second == "team-1")
				{
					assigned = app.screen_action(control.first);
					break;
				}
			if (!assigned)
				throw std::runtime_error("Selected-compartment team assignment control unavailable");
			app.session->tick();
			frame();
			frame();
			const auto support_view = app.session->observation();
			bool destination = false;
			for (const auto &team : support_view->gameplay.at("combat").at("teams"))
				if (team.at("id") == "team-1" && team.at("destination") == 0)
					destination = true;
			if (!destination)
				throw std::runtime_error("Repair team assignment did not reach physical mechanism");
			TakeScreenshot("combat-support.png");
			close_window();
			return 0;
		}
		TakeScreenshot("combat-helm.png");
		if (!app.screen_action("console/1/game.console/tactical"))
		{
			for (const auto &c : app.screen_controls)
				std::cerr << c.first << " " << c.second << "\n";
			std::cerr << "screen " << static_cast<int>(app.screen) << " observation "
			          << app.session->observation()->console << " additional "
			          << app.session->observation()->additional_consoles.size() << "\n";
			throw std::runtime_error("Tactical Console handover unavailable");
		}
		frame();
		frame();
		const auto own = app.session->observation();
		const Observation *tactical = nullptr;
		for (const auto &view : own->additional_consoles)
			if (view.console == "tactical")
				tactical = &view;
		if (!tactical)
			throw std::runtime_error("Human-seeking Tactical projection unavailable");
		const auto contact = std::find_if(tactical->contacts.begin(), tactical->contacts.end(),
		                                  [](const auto &c)
		                                  {
			                                  return c.radar_icon == RadarIcon::ship;
		                                  });
		if (contact == tactical->contacts.end())
			throw std::runtime_error("Pirate ship contact unavailable");
		if (!app.screen_action("console/1/game.combat-target/" + std::to_string(contact->id)))
			throw std::runtime_error("Select target action unavailable");
		frame();
		bool fired = false;
		for (const auto &controller : tactical->gameplay.at("controllers"))
			for (auto it = controller.at("commands").begin(); it != controller.at("commands").end(); ++it)
				if (it.value().at("action") == "weapon_target")
				{
					OperatingCommand command{own->own.id,
					                         "weapon_target",
					                         static_cast<std::int64_t>(contact->id),
					                         {},
					                         controller.at("id"),
					                         it.key(),
					                         controller.at("generation")};
					command.console = "tactical";
					if (!app.session->command(command))
						throw std::runtime_error("Tactical fire request refused");
					fired = true;
				}
		if (!fired)
			throw std::runtime_error("No armed Tactical Controller");
		if (mvp)
		{
			frame();
			if (!app.screen_action("console/1/game.status/beam"))
				throw std::runtime_error("System status light unavailable");
			frame();
			frame();
			TakeScreenshot("combat-system-popup.png");
			if (!app.screen_action("console/1/game.status-close"))
				throw std::runtime_error("System status popup close unavailable");
			frame();
			for (const auto &controller : tactical->gameplay.at("controllers"))
				for (auto it = controller.at("commands").begin(); it != controller.at("commands").end(); ++it)
					if (it.value().at("action") == "weapon_mode")
					{
						OperatingCommand command{own->own.id,
						                         "weapon_mode",
						                         1,
						                         {},
						                         controller.at("id"),
						                         it.key(),
						                         controller.at("generation")};
						command.console = "tactical";
						if (!app.session->command(command))
							throw std::runtime_error("Point defence Auto refused");
					}
		}
		app.session->tick();
		frame();
		frame();
		TakeScreenshot("combat-tactical.png");
		app.screen = Screen::viewscreen;
		if (destruction)
		{
			bool captured = false;
			for (int tick = 0; tick < 180; ++tick)
			{
				app.session->tick();
				frame();
				const auto scene = app.session->scene();
				if (std::any_of(scene.begin(), scene.end(),
				                [](const auto &b)
				                {
					                return !b.sound.empty();
				                }))
				{
					for (int n = 0; n < 6; ++n)
					{
						app.session->tick();
						frame();
					}
					TakeScreenshot("combat-explosion-growing.png");
					for (int n = 0; n < 16; ++n)
					{
						app.session->tick();
						frame();
					}
					TakeScreenshot("combat-explosion-fading.png");
					for (int n = 0; n < 30; ++n)
					{
						app.session->tick();
						frame();
					}
					const auto expired = app.session->scene();
					if (std::any_of(expired.begin(), expired.end(),
					                [](const auto &b)
					                {
						                return !b.sound.empty();
					                }))
						throw std::runtime_error("Explosion failed to expire");
					captured = true;
					break;
				}
			}
			if (!captured)
				throw std::runtime_error("No destruction during explicitly authored smoke setup");
			close_window();
			std::cout << "PASS authored destruction growth, removal, fade and session survival\n";
			return 0;
		}

		for (int tick = 0; tick < 60; ++tick)
		{
			app.session->tick();
			frame();
			if (mvp && tick == 20)
				TakeScreenshot("combat-missile-flight.png");
			if (mvp && tick == 35)
			{
				for (const auto &controller : tactical->gameplay.at("controllers"))
					for (auto it = controller.at("commands").begin(); it != controller.at("commands").end(); ++it)
						if (it.value().at("action") == "magazine_select")
						{
							OperatingCommand command{
							    own->own.id, "magazine_select",          1, {}, controller.at("id"),
							    it.key(),    controller.at("generation")};
							command.console = "tactical";
							if (!app.session->command(command))
								throw std::runtime_error("Havok magazine selection refused");
						}
			}
		}
		TakeScreenshot("combat-scene.png");
		bool turrets = false;
		for (const auto &body : app.session->scene())
			turrets |= !body.attachments.empty();
		if (!turrets)
			throw std::runtime_error("No articulated turret projection");
		close_window();
		std::cout << "PASS authored combat Console actions, turret projection and rendered screenshots\n";
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
