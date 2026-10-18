#include <iostream>
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
void require(bool result, const char *message)
{
	if (!result)
		throw std::runtime_error(message);
}
int main()
{
	try
	{
		PackDraft pack;
		pack.manifest = {{"schema", 1}, {"id", "test-core"}};
		pack.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
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
			auto ship = multi->observation()->own.id;
			require(multi->console_seats().size() == 2 && multi->claim_console(2, {ship, "sensors"}),
			        "session exposes and admits separate Console routes");
			ConsoleModel helm, sensors;
			helm.refresh(multi, 1);
			sensors.refresh(multi, 2);
			require(helm.layout() && sensors.layout() && helm.act("game.faster/widget") &&
			            helm.act("game.faster/widget"),
			        "headless model resolves authored layouts and accumulates same-tick actions");
			require(helm.requested_speed() > 0 && sensors.requested_speed() == 0 && !sensors.act("game.faster") &&
			            !helm.act("game.contacts"),
			        "each presentation model respects its own authority and state");
			require(sensors.act("game.contacts/radar") && !sensors.announcement().empty(),
			        "radar semantics use the permitted projection");
			require(multi->console_command(2, {ship, "radar_enabled", 0}) &&
			            !multi->console_command(2, {ship, "speed_mm_s", 1}),
			        "session route preserves System authority");
			multi->tick();
			require(multi->inspection().at("bodies").size() == multi->viewscreen().size(),
			        "trusted local GM inspection exposes the live world beside independent Console routes");
			helm.refresh(multi);
			require(helm.requested_speed() == multi->observation()->requested_speed,
			        "model reconciles pending requests with committed observations");
			auto recovered = make_local_session();
			recovered->restore(multi->save());
			require(recovered->recovery_pending() && !recovered->console_observation(2),
			        "portable recovery clears all Console routes");
			recovered->resume_local();
			require(recovered->observation()->console == "helm" && recovered->claim_console(2, {ship, "sensors"}) &&
			            !recovered->console_observation(2)->radar_enabled,
			        "schema-2 recovery preserves selection and System state with fresh claims");
			sensors.refresh(recovered, 2);
			require(sensors.announcement().empty(), "model clears old announcements when a session is replaced");
			recovered->release_console(2);
			sensors.refresh(recovered, 2);
			require(!sensors.observation() && !sensors.layout() && !sensors.act("game.contacts"),
			        "lost claim removes Console content and actions");
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
			require(invoke_navigation(app, "settings.quit") && app.screen == Screen::home && !game_active(app) &&
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
		require(state(session->save()) == state(recovered->save()),
		        "Fresh console recovery must preserve tick, motion, requests, remainders and delegated work");
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
		std::cout << "PASS local setup, rollback, admission, projections and portable paused recovery\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
