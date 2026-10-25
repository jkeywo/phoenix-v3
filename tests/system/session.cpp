#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

import phoenix.session.api;
import phoenix.session.local;
import phoenix.console;
import phoenix.navigation;

using namespace phoenix;
using phoenix::test::require;
int main()
{
	try
	{
		auto pack =
		    phoenix::test::fixture_pack(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "test-core");
		{
			// A portable save contains multiple individually bounded content packs.
			// Incompressible synthetic assets exercise the envelope, not production art.
			auto large_base = pack;
			PackDraft extra;
			extra.manifest = {{"schema", 1}, {"id", "test-save-assets"}, {"dependencies", {"test-core"}}};
			extra.files["definitions.json"] = json_bytes(Json{{"schema", 1}});
			std::uint32_t random = 0x9e3779b9;
			for (int n = 0; n < 3; ++n)
			{
				Bytes payload(11 * 1024 * 1024);
				for (auto &byte : payload)
				{
					random ^= random << 13;
					random ^= random >> 17;
					random ^= random << 5;
					byte = static_cast<std::uint8_t>(random);
				}
				large_base.files["assets/save-" + std::to_string(n) + ".bin"] = payload;
				extra.files["assets/save-" + std::to_string(n) + ".bin"] = std::move(payload);
			}
			auto original = make_local_session();
			original->add_pack(write_pack(large_base));
			original->add_pack(write_pack(extra));
			original->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
			original->tick();
			const auto portable_large = original->save();
			require(portable_large.size() > content_archive_limit,
			        "Portable fixture must cross the content-archive boundary");
			bool rejected = false;
			try
			{
				(void)read_pack(portable_large);
			}
			catch (const std::exception &)
			{
				rejected = true;
			}
			require(rejected, "Runtime-save allowance must not expand ordinary content limits");
			auto recovered_large = make_local_session();
			recovered_large->restore(portable_large);
			require(recovered_large->recovery_pending() &&
			            recovered_large->catalog().ordered_packs == original->catalog().ordered_packs,
			        "Large portable save must preserve exact nested content identities and paused recovery");
			recovered_large->resume_local();
			for (int n = 0; n < 5; ++n)
			{
				original->tick();
				recovered_large->tick();
			}
			require(original->ticks() == recovered_large->ticks() &&
			            original->observation()->own.x == recovered_large->observation()->own.x &&
			            original->observation()->own.y == recovered_large->observation()->own.y,
			        "Large portable save must continue through ordinary Session recovery");
		}

		{
			auto multi_pack = pack;
			auto data = parse_json(multi_pack.files.at("definitions.json"), "stations fixture");
			data["schema"] = 2;
			data["entities"][0].erase("console");
			data["consoles"].push_back({{"id", "test.empty"}, {"widgets", Json::array()}});
			data["entities"][0]["stations"] = Json::array(
			    {{{"id", "bridge"},
			      {"consoles",
			       Json::array({{{"id", "helm"}, {"definition", "test.empty"}, {"systems", {"engine", "steering"}}},
			                    {{"id", "sensors"}, {"definition", "test.empty"}, {"systems", {"radar"}}}})}}});
			multi_pack.files["definitions.json"] = json_bytes(data);
			auto multi = make_local_session();
			multi->add_pack(write_pack(multi_pack));
			multi->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot", "helm"});
#ifndef __EMSCRIPTEN__
			Application player;
			player.session = multi;
			player.screen = Screen::settings;
			player.settings_return = Screen::console;
			player.settings_tab = "session";
			bool quit = false;
			player.return_home = [&]
			{
				quit = true;
			};
			require(!invoke_navigation(player, "settings.quit") && !quit && multi->observation().has_value(),
			        "Native player settings reject stale quit actions without abandoning the station");
#endif
			auto ship = multi->observation()->own.id;
			require(multi->console_seats().size() == 2 && multi->claim_console(2, {ship, "sensors"}),
			        "session exposes and admits separate Console routes");
			ConsoleModel helm, sensors;
			helm.refresh(multi, 1);
			sensors.refresh(multi, 2);
			const auto layout_revision = helm.layout_revision();
			require(helm.layout() && sensors.layout() && helm.act("game.faster/widget") &&
			            helm.act("game.faster/widget"),
			        "headless model resolves authored layouts and accumulates same-tick actions");
			require(helm.requested_speed() > 0 && sensors.requested_speed() == 0 && !sensors.act("game.faster") &&
			            !helm.act("game.contacts"),
			        "each presentation model respects its own authority and state");
			require(helm.set_speed_fraction(.65) && helm.set_heading_degrees(123.25) && !helm.set_speed_fraction(-.1) &&
			            !helm.set_speed_fraction(1.1) && !helm.set_heading_degrees(360) &&
			            !sensors.set_speed_fraction(.5) && !sensors.set_heading_degrees(90),
			        "Absolute slider/ring controls preserve value bounds and per-Console authority");
			require(sensors.act("game.contacts/radar") && !sensors.announcement().empty(),
			        "radar semantics use the permitted projection");
			require(multi->console_command(2, {ship, "radar_enabled", 0}) &&
			            !multi->console_command(2, {ship, "speed_mm_s", 1}),
			        "session route preserves System authority");
			multi->tick();
			require(multi->observation()->requested_heading == 123250 &&
			            multi->observation()->requested_speed == helm.maximum_speed() * 65 / 100,
			        "Absolute Helm controls commit through the same production session command path");
			require(multi->inspection().at("bodies").size() == multi->viewscreen().size(),
			        "trusted local GM inspection exposes the live world beside independent Console routes");
			helm.refresh(multi);
			require(helm.requested_speed() == multi->observation()->requested_speed,
			        "model reconciles pending requests with committed observations");
			require(helm.layout_revision() == layout_revision,
			        "committed operating values preserve the resolved layout identity");
			auto recovered = make_local_session();
			recovered->restore(multi->save());
			require(recovered->recovery_pending() && !recovered->console_observation(2),
			        "portable recovery clears all Console routes");
			recovered->resume_local();
			require(recovered->observation()->console == "helm" && recovered->claim_console(2, {ship, "sensors"}) &&
			            !recovered->console_observation(2)->radar_enabled,
			        "schema-2 recovery preserves selection and System state with fresh claims");
			const auto previous_layout_revision = sensors.layout_revision();
			sensors.refresh(recovered, 2);
			require(sensors.announcement().empty(), "model clears old announcements when a session is replaced");
			require(sensors.layout_revision() != previous_layout_revision,
			        "replacement sessions invalidate cached Console layouts");
			recovered->release_console(2);
			sensors.refresh(recovered, 2);
			require(!sensors.observation() && !sensors.layout() && !sensors.act("game.contacts"),
			        "lost claim removes Console content and actions");
		}
		{
			auto instances = pack;
			auto data = parse_json(pack.files.at("definitions.json"), "instance Console fixture");
			auto &ship = data["entities"][0];
			ship["systems"] = {
			    {"port", {{"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}}},
			    {"starboard", {{"kind", "engine"}, {"max_speed_mm_s", 20000}, {"acceleration_mm_s2", 2000}}},
			    {"engine", {{"kind", "steering"}, {"turn_mdeg_s", 90000}}},
			    {"search", {{"kind", "radar"}, {"range_mm", 100000}}}};
			auto &widgets = data["consoles"][0]["widgets"];
			widgets[0]["system"] = "search";
			widgets[1]["system"] = "port";
			widgets[2]["system"] = "engine";
			auto extra = widgets[1];
			extra["id"] = "starboard-slider";
			extra["system"] = "starboard";
			widgets.push_back(extra);
			instances.files["definitions.json"] = json_bytes(data);
			auto session = make_local_session();
			session->add_pack(write_pack(instances));
			session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
			ConsoleModel model;
			model.refresh(session);
			const auto snapshot = session->console_snapshot(1);
			require(snapshot && snapshot == session->console_snapshot(1) && model.observation() == snapshot,
			        "The Console model and other presentation consumers share one permitted snapshot");
			require(model.widget_system("engine") == "port" && model.widget_system("starboard-slider") == "starboard" &&
			            model.widget_system("radar") == "search" && model.system_maximum("starboard") == 20000,
			        "widget identity resolves exact fitted capability and public bounds");
			require(model.set_speed_fraction(.2, "port") && model.set_speed_fraction(.7, "starboard") &&
			            model.set_heading_degrees(33, "engine") && !model.set_speed_fraction(.5),
			        "explicit widget commands admit independently while ambiguous implicit control refuses");
			require(model.system_request("port") == 2000 && model.system_request("starboard") == 14000,
			        "same-tick pending feedback is independent per fitted instance");
			require(session->console_snapshot(1) != snapshot,
			        "Admitted same-tick commands invalidate presentation snapshots immediately");
			session->tick();
			model.refresh(session);
			require(model.act("game.faster/engine"), "semantic action resolves its widget's System");
			session->tick();
			model.refresh(session);
			require(model.system_request("port") == 2500 && model.system_request("starboard") == 14000 &&
			            model.system_request("engine") == 33000,
			        "widget actions use their own request and maximum rather than a sibling's state");
			auto resumed = make_local_session();
			resumed->restore(session->save());
			resumed->resume_local();
			ConsoleModel recovered;
			recovered.refresh(resumed);
			require(recovered.system_request("port") == 2500 && recovered.system_request("starboard") == 14000,
			        "portable instance recovery preserves operations and restores fresh Console selection");
		}
		auto session = make_local_session();
		require(!session->command({1, "speed_mm_s", 1}) && !session->observation(),
		        "Idle session must expose no claimed projection or commands");
		session->add_pack(write_pack(pack));
		auto identity = session->catalog().ordered_packs;
		bool refused = false;
		try
		{
			session->add_pack(Bytes{1, 2, 3});
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && session->catalog().ordered_packs == identity,
		        "A failed import must preserve the selected content");
		session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
		const auto started_generation = session->generation();
		auto own = session->observation()->own.id;
		{
			Application app;
			app.session = session;
			app.can_host = true;
			app.native_menu = true;
			app.screen = Screen::viewscreen;
			app.screen_action = [](const std::string &)
			{
				return true;
			};
			const auto controls = navigation_controls(app);
			require(controls.size() == 1 && controls.front().id == "settings",
			        "A running Viewscreen exposes only Settings through shared navigation");
			for (const char *action : {"home", "lobby", "host", "join", "view", "edit", "locale", "game.console",
			                           "game.scene", "game.follow", "game.local-gm"})
				require(!invoke_navigation(app, action) && app.screen == Screen::viewscreen,
				        "Stale navigation and role-switch actions cannot leave or change a running game");
			require(invoke_navigation(app, "settings") && app.screen == Screen::settings &&
			            !invoke_navigation(app, "lobby") && invoke_navigation(app, "settings.close") &&
			            app.screen == Screen::viewscreen && app.session == session,
			        "Settings closes back to the same game without re-entering the lobby");
			app.can_host = false;
			app.native_menu = false;
			require(invoke_navigation(app, "settings") && set_music_volume(app, "35"),
			        "Connected player clients can use their own Settings");
			auto replacement = make_local_session();
			app.return_home = [&]
			{
				app.session = replacement;
				app.screen = Screen::home;
			};
			require(!invoke_navigation(app, "settings.quit") && invoke_navigation(app, "settings.tab/session") &&
			            invoke_navigation(app, "settings.quit") && app.screen == Screen::home && !game_active(app) &&
			            !app.session->observation() && !app.session->running(),
			        "Explicit quit returns to a fresh main-menu session without a running-world claim");
		}
		require(session->viewscreen().size() == 2 && own != 1,
		        "Selected slot must create and claim its own ship, not a map entity");
		require(!session->command({1, "speed_mm_s", 1000}), "Local adapter must refuse another ship's commands");
		require(session->command({own, "speed_mm_s", 1000}), "Local adapter must admit an owned engine request");
		auto before = session->observation()->own.x;
		for (int i = 0; i < 20; ++i)
			session->tick();
		require(session->observation()->own.x > before && session->ticks() == 20,
		        "The admitted command must execute through the real fixed-tick simulation");
		refused = false;
		try
		{
			session->start({"test.map", {{"test.slot", "missing"}}, "test.slot"});
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && session->generation() == started_generation && session->ticks() == 20 &&
		            session->observation()->own.x > before,
		        "Failed setup must preserve the running world");
		refused = false;
		try
		{
			session->add_pack(write_pack(pack));
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Running content must not be hot-swapped");
		auto portable = session->save();
		auto recovered = make_local_session();
		recovered->restore(portable);
		require(recovered->recovery_pending() && !recovered->running() && !recovered->observation(),
		        "Portable recovery must pause without restoring a live console claim");
		auto paused_tick = recovered->ticks();
		recovered->tick();
		require(recovered->ticks() == paused_tick && !recovered->command({own, "speed_mm_s", 1000}),
		        "Recovery must not advance or admit commands before an explicit reclaim");
		require(recovered->catalog().ordered_packs == identity,
		        "Portable save must carry the exact original pack identities without installed content");
		recovered->resume_local();
		for (int i = 0; i < 20; ++i)
		{
			session->tick();
			recovered->tick();
		}
		auto state = [](const Bytes &archive)
		{
			return parse_json(read_pack(archive).files().at("session.json"), "session.json").at("state");
		};
		const auto uninterrupted = state(session->save());
		auto resumed = state(recovered->save());
		for (std::size_t body = 0; body < uninterrupted.at("bodies").size(); ++body)
		{
			const auto &original = uninterrupted.at("bodies")[body];
			if (!original.contains("console_controllers"))
				continue;
			const auto &controllers = original.at("console_controllers").at("controllers");
			for (std::size_t i = 0; i < controllers.size(); ++i)
			{
				const auto generation = controllers[i].at("generation").get<std::uint64_t>();
				auto &recovered_generation =
				    resumed["bodies"][body]["console_controllers"]["controllers"][i]["generation"];
				const auto transitions = controllers[i].at("human").get<bool>() ? 2 : 0;
				require(recovered_generation.get<std::uint64_t>() == generation + transitions,
				        "Portable clear/reclaim invalidates human Controller generations without changing AI owners");
				recovered_generation = generation;
			}
		}
		require(uninterrupted == resumed, "Fresh recovery preserves all physical and Controller continuation state "
		                                  "apart from fresh authority generations");
		auto server = make_host_world();
		server->restore(portable);
		server->resume_server();
		require(server->running() && !server->observation() && !server->command({own, "speed_mm_s", 1000}),
		        "Server recovery must activate simulation without claiming its local console");
		require(server->focus_body() && server->focus_body()->id == own,
		        "Viewscreen own-ship focus must remain available without claiming a Console");
		require(server->authoritative_world().can_claim(1, own),
		        "Server activation must leave the station available for real admission");
		server->tick();
		require(server->ticks() == paused_tick + 1, "Server recovery must resume authoritative ticks");
		auto corrupt = read_pack(portable).draft();
		auto data = parse_json(corrupt.files.at("session.json"), "session.json");
		data["state"]["packs"][0][1] = "wrong-hash";
		corrupt.files["session.json"] = json_bytes(data);
		auto before_restore = state(recovered->save());
		const auto recovered_generation = recovered->generation();
		refused = false;
		try
		{
			recovered->restore(write_pack(corrupt));
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && recovered->generation() == recovered_generation &&
		            state(recovered->save()) == before_restore,
		        "A mismatched portable save must leave the running session unchanged");
		recovered->restore(portable);
		require(recovered->generation() != recovered_generation && recovered->catalog().ordered_packs == identity,
		        "Replacing a world with identical packs must invalidate session presentation state");
		const auto replaced_generation = recovered->generation();
		recovered->resume_local();
		recovered->tick();
		require(recovered->generation() == replaced_generation,
		        "Console admission and ordinary ticks must preserve the session generation");
		session->clear_packs();
		require(session->generation() != started_generation && !session->running() && !session->observation() &&
		            session->catalog().ordered_packs.empty(),
		        "Reset must release authority and content together");
#if !defined(__EMSCRIPTEN__) || defined(PHOENIX_BROWSER_THREADS)
		{
			// Synthetic production-loader fixture: concurrency must preserve the same
			// fixed-tick state and immutable readers; elapsed time is only a hang guard.
			auto worker = make_frame_session(make_local_session());
			worker->add_pack(write_pack(pack));
			const GameSetup setup{"test.map", {{"test.slot", "test.ship"}}, "test.slot"};
			worker->start(setup);
			const auto retained = worker->console_snapshot(1);
			require(retained != nullptr, "Worker start must publish its admitted Console");
			const auto retained_gameplay = retained->gameplay;
			worker->tick();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (worker->ticks() < 3 && std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				worker->poll();
			}
			const auto checkpoint = pause_frame_session_checkpoint(worker);
			const auto worker_state = state(checkpoint);
			require(worker->ticks() >= 3 && retained->gameplay == retained_gameplay,
			        "Worker must advance fixed ticks without modifying a retained reader");
			auto serial = make_local_session();
			serial->add_pack(write_pack(pack));
			serial->start(setup);
			for (std::uint64_t tick = 0; tick < worker->ticks(); ++tick)
				serial->tick();
			require(state(serial->save()) == worker_state, "Worker continuation must match serial execution exactly");
			const OperatingCommand queued{retained->own.id, "speed_mm_s", 1200};
			require(worker->dispatch_console_command(1, queued), "UI command delivery must enter the worker queue");
			require(serial->console_command(1, queued), "Synthetic owned engine command must be admissible");
			const OperatingCommand refused{retained->own.id + 9999, "speed_mm_s", 1200};
			require(worker->dispatch_console_command(1, refused) && !serial->console_command(1, refused),
			        "Queued delivery must leave foreign-target admission to the authoritative session");
			const auto queued_tick = worker->ticks();
			worker->tick();
			const auto command_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (worker->ticks() <= queued_tick && std::chrono::steady_clock::now() < command_deadline)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				worker->poll();
			}
			const auto command_checkpoint = pause_frame_session_checkpoint(worker);
			while (serial->ticks() < worker->ticks())
				serial->tick();
			require(state(command_checkpoint) == state(serial->save()),
			        "Queued commands and refusals must match ordered authoritative execution");
			require(frame_session_profile(worker).at("refused_commands") == 1,
			        "The worker must record the authoritative refusal after queued delivery");
			require(worker->release_console(1) && !worker->console_snapshot(1) &&
			            !worker->dispatch_console_command(1, queued),
			        "Worker release must synchronously retire admission");
			require(worker->claim_console(1, {retained->own.id, retained->console}),
			        "Worker route reuse must pass real admission");
			const auto before_clear = worker->generation();
			worker->clear_packs();
			require(worker->generation() != before_clear && !worker->running() && !worker->console_snapshot(1),
			        "Worker reset must retire content and projections together");
		}
#endif
		std::cout << "PASS local setup, rollback, admission, projections and portable paused recovery\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
