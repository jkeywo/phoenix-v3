// Explicit production-content exercise. Never a CTest balance assertion.
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.session.local;
import phoenix.presentation;
using namespace phoenix;
#include "cruiser_transport.hpp"

int main(int argc, char **argv)
{
	bool window = false;
	Json receipt;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_cruiser_smoke pack.zip [--headless] [maximum-ticks]");
		const bool withdrawal = argc > 2 && std::string(argv[2]) == "--withdrawal";
		const bool combat_frame = argc > 2 && std::string(argv[2]) == "--frame";
		const bool headless = withdrawal || (argc > 2 && std::string(argv[2]) == "--headless");
		const int maximum = argc > 3 ? std::stoi(argv[3]) : 12000;
		const auto catalog = resolve_packs({read_pack(read_file(argv[1]))});
		if (argc > 2 && std::string(argv[2]) == "--transport")
		{
			cruiser_transport(catalog);
			return 0;
		}
		World world;
		if (withdrawal || combat_frame)
			world.restore(catalog, parse_json(read_file("build/cruiser-midwave.json"), "Cruiser mid-wave smoke save"));
		else
			world.load(catalog, "base.cruiser-combat", {{"cruiser", "base.cruiser"}});
		const auto ship = *world.ship_for_slot("cruiser");
		receipt["ship"] = ship;
		std::uint64_t station_claim = 910;
		for (const auto *station : {"captain", "helm", "tactical", "engineering", "science", "comms"})
		{
			if (!world.claim(station_claim, ship, station))
				throw std::runtime_error(std::string("Primary Cruiser station unavailable: ") + station);
			receipt["stations"].push_back(station);
			++station_claim;
		}
		for (std::uint64_t claim = 910; claim < station_claim; ++claim)
			world.release(claim);
		std::unique_ptr<SceneRenderer> renderer;
		if (!headless)
		{
			InitWindow(1600, 900, "Cruiser material and combat exercise");
			window = true;
			renderer = std::make_unique<SceneRenderer>();
			renderer->preload(catalog, "base.cruiser");
		}
		auto capture = [&](const std::string &name, bool gallery, double light = 0., std::int64_t traverse = 0)
		{
			if (!renderer)
				return;
			auto bodies = world.scene();
			const auto own = std::find_if(bodies.begin(), bodies.end(),
			                              [&](const auto &b)
			                              {
				                              return b.id == ship;
			                              });
			if (own == bodies.end())
				return;
			const auto own_copy = *own;
			if (gallery)
				bodies = {own_copy};
			for (auto &body : bodies)
				for (auto &attachment : body.attachments)
					attachment.heading += traverse;
			BeginDrawing();
			ClearBackground({10, 16, 24, 255});
			for (int i = 0; i < (gallery ? 8 : 1); ++i)
			{
				const double angle = i * 3.14159265358979323846 / 4;
				const float distance = gallery ? (name == "engines" ? 650.f : 500.f) : 900.f;
				Vector3 centre{float(own_copy.x / 1000.), 15, float(-own_copy.y / 1000.)};
				Camera3D camera{
				    {centre.x + float(std::cos(angle)) * distance, 100, centre.z - float(std::sin(angle)) * distance},
				    centre,
				    {0, 1, 0},
				    40,
				    CAMERA_PERSPECTIVE};
				Rectangle region =
				    gallery ? Rectangle{float(i % 4 * 400), float(i / 4 * 450), 400, 450} : Rectangle{0, 0, 1600, 900};
				renderer->draw(catalog, bodies, camera, region,
				               {{"sun_direction", {std::cos(light) * .7, .6, std::sin(light) * .7}},
				                {"ambient", .35},
				                {"shadows", true}},
				               world.ticks() / 20.);
				DrawText((gallery ? std::to_string(i * 45) + " degrees" : name).c_str(), int(region.x) + 8,
				         int(region.y) + 8, 20, RAYWHITE);
			}
			auto captured = LoadImageFromScreen();
			ExportImage(captured, ("build/cruiser-" + name + ".png").c_str());
			UnloadImage(captured);
			EndDrawing();
		};
		auto craft_orders = [&](bool recall)
		{
			if (!world.claim(901, ship, "tactical"))
				throw std::runtime_error("Tactical claim refused");
			auto observation = *world.observe(901);
			for (const auto &craft : observation.gameplay.at("craft"))
			{
				if (craft.at("role") == "shuttle")
					continue;
				OperatingCommand order{ship,
				                       recall                         ? "craft.recall"
				                       : craft.at("role") == "bomber" ? "craft.bomb"
				                                                      : "craft.defend",
				                       craft.at("revision"), craft.at("id")};
				order.console = "tactical";
				order.controller = craft.at("bay");
				for (const auto &controller : observation.gameplay.at("controllers"))
					if (controller.at("id") == order.controller)
						order.generation = controller.at("generation");
				if (!recall && craft.at("role") == "bomber")
					for (const auto &contact : observation.contacts)
						if (contact.disposition == "hostile")
						{
							order.text = std::to_string(contact.id);
							break;
						}
				const bool accepted = world.submit(901, order);
				receipt[recall ? "recall" : "sorties"].push_back({{"craft", craft.at("id")}, {"accepted", accepted}});
			}
			world.tick();
			world.release(901);
		};
		capture("bearings", true);
		if (renderer)
		{
			capture("lighting-port", true, 1.57, 45000);
			capture("lighting-aft", true, 3.14, 90000);
			capture("lighting-starboard", true, 4.71, 135000);
			if (combat_frame)
			{
				if (!world.claim(909, ship, "helm"))
					throw std::runtime_error("Helm pulse claim refused");
				const auto helm = *world.observe(909);
				for (const auto *engine : {"drive.engine", "wing.engine"})
				{
					OperatingCommand command{ship, "longitudinal_thrust", 1000, "", engine, "longitudinal_thrust"};
					command.console = "helm";
					for (const auto &control : helm.gameplay.at("controllers"))
						if (control.at("id") == engine)
							command.generation = control.at("generation");
					if (!world.submit(909, command))
						throw std::runtime_error("Helm pulse order refused");
				}
				for (int i = 0; i < 20; ++i)
				{
					PollInputEvents();
					world.tick();
				}
				capture("engines", true);
				capture("combat", false);
				bool burning = false;
				for (const auto &body : world.scene())
					if (body.id == ship)
					{
						burning = body.forward_thrust > 0;
						std::ofstream("build/cruiser-engine-pulse.json") << Json{
						    {"forward_thrust", body.forward_thrust},
						    {"vx", body.vx},
						    {"vy", body.vy},
						    {"tick", world.ticks()}}.dump(2);
					}
				world.release(909);
				if (!burning)
					throw std::runtime_error("Helm pulse produced no actual forward thrust");
			}
		}
		if (maximum == 0 && !withdrawal)
		{
#ifndef __EMSCRIPTEN__
			renderer.reset();
			if (window)
				CloseWindow();
#endif
			return 0;
		}
		for (int n = 0; n < (withdrawal ? 0 : maximum); ++n)
		{
			if (window)
				PollInputEvents();
			world.tick();
			if (world.ticks() == 260)
				craft_orders(false);
			if (world.ticks() == 1200)
				craft_orders(true);
			if (world.ticks() == 600)
			{
				const auto saved = world.save();
				std::ofstream("build/cruiser-midwave.json") << saved.dump(2);
				World resumed;
				resumed.restore(catalog, saved);
				std::ofstream("build/cruiser-restore-diff.json") << Json::diff(saved, resumed.save()).dump(2);
				for (int i = 0; i < 40; ++i)
				{
					if (window)
						PollInputEvents();
					world.tick();
					resumed.tick();
				}
				receipt["continuation_equal"] = world.save() == resumed.save();
				if (!receipt.at("continuation_equal").get<bool>())
				{
					std::ofstream("build/cruiser-continuation-diff.json")
					    << Json::diff(world.save(), resumed.save()).dump(2);
					std::ofstream("build/cruiser-uninterrupted.json") << world.save().dump(2);
					std::ofstream("build/cruiser-resumed.json") << resumed.save().dump(2);
					throw std::runtime_error("Cruiser continuation differs after mid-wave recovery");
				}
			}
			if (world.ticks() % 200 == 0)
			{
				const auto saved = world.save();
				const auto scenario = saved.at("scenario").at("runtime");
				receipt["craft_history"].push_back({{"tick", world.ticks()}, {"craft", saved.at("craft")}});
				receipt["tick"] = world.ticks();
				receipt["scenario"] = scenario;
				std::ofstream("build/cruiser-progress.json") << receipt.dump(2);
				std::cout << "Tick " << world.ticks() << ": " << world.inspect().size() << " active bodies\n"
				          << std::flush;
				if (world.ticks() % 1000 == 0)
					capture(std::to_string(world.ticks()), false);
				bool finished = false;
				for (const auto &instance : scenario.at("instances"))
					if (instance.at("name") == "main" && instance.at("finished").get<bool>())
						finished = true;
				if (finished)
					break;
			}
		}
		capture("final", false);
		const auto encounter = world.save().at("scenario").at("runtime");
		receipt["victory"] = false;
		for (const auto &instance : encounter.at("instances"))
			if (instance.at("name") == "main")
				receipt["victory"] = instance.at("succeeded");
		if (!withdrawal && !receipt.at("victory").get<bool>())
			throw std::runtime_error("Cruiser exercise ended without clearing all three waves");
		craft_orders(true);
		for (int i = 0; i < 1200; ++i)
			world.tick();
		world.claim(901, ship, "tactical");
		receipt["craft_final"] = world.observe(901)->gameplay.at("craft");
		receipt["all_live_recovered"] = true;
		for (const auto &craft : receipt.at("craft_final"))
			if (craft.at("phase") != "docked" && craft.at("phase") != "lost")
				receipt["all_live_recovered"] = false;
		world.release(901);
		if (!withdrawal && !receipt.at("all_live_recovered").get<bool>())
			throw std::runtime_error("Live Cruiser craft did not complete recovery before departure");
		if (!world.claim(902, ship, "science"))
			throw std::runtime_error("Science claim refused for departure check");
		auto navigator = [&]()
		{
			const auto observation = *world.observe(902);
			for (const auto &extra : observation.additional_consoles)
				if (extra.console == "navigator")
					return extra;
			throw std::runtime_error("Navigator auxiliary Console unavailable");
		};
		auto navigator_order = [&](bool depart)
		{
			const auto observation = navigator();
			OperatingCommand order{ship,           depart ? "ftl.depart" : "system_enabled",
			                       depart ? 0 : 1, depart ? "ftl.drive" : "",
			                       "ftl.drive",    depart ? "" : "enabled"};
			order.console = "navigator";
			for (const auto &controller : observation.gameplay.at("controllers"))
				if (controller.at("id") == "ftl.drive")
					order.generation = controller.at("generation");
			return world.submit(902, order);
		};
		if (!navigator_order(false))
			throw std::runtime_error("FTL charge order refused");
		for (int i = 0; i < 1800 && !navigator().gameplay.at("ftl")[0].at("ready").get<bool>(); ++i)
			world.tick();
		if (!navigator_order(true))
			throw std::runtime_error("Powered Cruiser departure never became executable");
		world.tick();
		receipt["departure"] = world.observe(902)->gameplay.value("mission_report", Json());
		if (receipt.at("departure").is_null())
			throw std::runtime_error("Cruiser departure produced no mission report");
		receipt["final"] = world.save();
		std::ofstream(withdrawal ? "build/cruiser-withdrawal.json" : "build/cruiser-receipt.json") << receipt.dump(2);
		renderer.reset();
		if (window)
			CloseWindow();
		std::cout << "Cruiser exercise recorded in build/cruiser-receipt.json\n";
	}
	catch (const std::exception &e)
	{
		receipt["error"] = e.what();
		const std::string mode = argc > 2 ? argv[2] : "";
		std::ofstream(mode == "--transport"    ? "build/cruiser-transport-error.json"
		              : mode == "--withdrawal" ? "build/cruiser-withdrawal.json"
		              : mode == "--frame"      ? "build/cruiser-frame-error.json"
		                                       : "build/cruiser-receipt.json")
		    << receipt.dump(2);
		if (window)
			CloseWindow();
		std::cerr << e.what() << '\n';
		return 1;
	}
}
