#include "../support/assertions.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.network.lobby;
import phoenix.network.lobby.protocol;
import phoenix.network.host;
import phoenix.network.client;
import phoenix.session.local;
import phoenix.fleet_setup;
import phoenix.console;
using namespace phoenix;
using phoenix::test::require;
int main(int argc, char **argv)
{
	try
	{
		const auto short_code = new_join_code();
		require(short_code.size() == 6 &&
		            short_code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") == std::string::npos,
		        "Host codes contain six uppercase alphanumeric characters");
		require(parse_join_code("aBc123").room == "ABC123" && parse_join_code("aBc123").server == "host" &&
		            display_join_code("ABC123", "host") == "ABC123" && parse_join_code("legacy/ship").server == "ship",
		        "Short codes normalize for admission while existing direct invitations remain usable");
		if (argc != 2)
			throw std::runtime_error("Supply the private synthetic signalling URL");
		NetworkOptions options;
		options.link.url = argv[1];
		options.link.room = "lobby-" + random_token();
		options.link.self = "organiser";
		options.link.force_relay = true;
		options.data_location = "lobby-integration-" + random_token();
		auto host = make_game_lobby(options);
		PackDraft pack;
		pack.manifest = {{"schema", 1}, {"id", "test-core"}};
		auto defs = parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic lobby");
		defs["schema"] = 4;
		defs["knowledge"] = Json::parse(R"json({"roles":["crew"],"schemas":[
		 {"id":"test.public","producers":["crew"],"readers":["crew"],"ship_publishable":true,
		  "fields":[{"id":"enabled","type":"boolean","publishable":true}]}]})json");
		auto &public_ship = defs["entities"][0];
		public_ship.erase("console");
		public_ship["stations"] = Json::parse(R"json([{ "id":"bridge","consoles":[
		 {"id":"main","definition":"test.console","systems":["engine","steering","radar"],"knowledge_roles":["crew"]}]}])json");
		public_ship["systems"]["engine"]["records"] = Json::parse(R"json([
		 {"schema":"test.public","producer":"crew","fields":{"enabled":"enabled"}}])json");
		public_ship["systems"]["reserve"] = {
		    {"kind", "engine"}, {"max_speed_mm_s", 17000}, {"acceleration_mm_s2", 1000}};
		public_ship["stations"][0]["consoles"][0]["systems"].push_back("reserve");
		public_ship["publications"] = Json::parse(R"json([
		 {"id":"engine-enabled","source_console":"main","schema":"test.public","subject":0,"fields":["enabled"],"live":true}])json");
		public_ship["control_layout"] = Json::parse(R"json({"schema":1,"default_profile":"direct",
		 "controllers":[{"id":"propulsion","console":"main","fallback":"hold","commands":[
		  {"id":"speed","action":"speed_mm_s","consumers":["engine","reserve"]}]}],
		 "profiles":[{"id":"direct","consoles":{"main":{"human":["propulsion"],"ai":[]}}},
		 {"id":"automatic","consoles":{"main":{"human":[],"ai":["propulsion"]}}}]})json");
		defs["consoles"][0]["widgets"][1]["controller"] = "propulsion";
		defs["consoles"][0]["widgets"][1]["channel"] = "speed";
		defs["knowledge"]["schemas"].push_back({{"id", "test.report"},
		                                        {"producers", {"crew"}},
		                                        {"readers", {"crew"}},
		                                        {"fields", Json::array({{{"id", "amount"}, {"type", "integer"}}})}});
		defs["messages"] = Json::parse(
		    R"json([{"id":"test.high","schema":"test.report","title_key":"test.high","body_key":"test.high.body",
		 "destination":{"kind":"controller","console":"main","id":"propulsion"},"delay_ticks":1,"expires_after_ticks":1000,
		 "fields":{"amount":{"argument":"amount"}},"parameters":{},"automatic":false}])json");
		public_ship["message_templates"] = {"test.high"};
		public_ship["systems"]["engine"]["reporters"] = Json::parse(R"json([
		 {"id":"high","field":"requested","above":4000,"message":"test.high","debounce_ticks":1}])json");
		const auto original = defs["maps"][0]["ship_slots"][0];
		for (const auto &id : {"second", "mandatory", "optional"})
		{
			auto slot = original;
			slot["id"] = id;
			slot["x_mm"] = id == std::string("second") ? 80000 : id == std::string("mandatory") ? 160000 : 240000;
			slot["mandatory"] = id == std::string("mandatory");
			defs["maps"][0]["ship_slots"].push_back(slot);
		}
		pack.files["definitions.json"] = json_bytes(defs);
		pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		host->add_pack(write_pack(pack));
		host->prepare_lobby({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
		std::vector<std::shared_ptr<SessionPort>> nodes{host};
		auto join = [&](const std::string &id, JoinRole role)
		{
			auto config = options;
			config.server = "organiser";
			config.link.self = id;
			config.profile = id;
			config.player_name = id == "crew" ? "Navigator" : id;
			if (id == "crew")
				config.avatar = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
			auto node = role == JoinRole::ship_server ? join_game_lobby(config, role) : join_client_lobby(config, role);
			nodes.push_back(node);
			return node;
		};
		auto pump = [&](const std::function<bool()> &condition, int limit = 20000)
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit);
			while (std::chrono::steady_clock::now() < deadline)
			{
				for (auto &node : nodes)
				{
					node->poll();
					node->tick();
				}
				if (condition())
					return;
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			for (const auto &node : nodes)
				std::cerr << node->connection_status() << " / " << node->lobby().status << '\n';
			throw std::runtime_error("Timed out waiting for lobby contract");
		};
		auto ship = join("ship-two", JoinRole::ship_server);
		auto rival = join("rival", JoinRole::ship_server);
		auto crew = join("crew", JoinRole::player);
		auto gm = join("inspector", JoinRole::gm);
		pump(
		    [&]
		    {
			    const auto lobby = host->lobby();
			    return lobby.members.size() == 5 && std::any_of(lobby.members.begin(), lobby.members.end(),
			                                                    [](const auto &member)
			                                                    {
				                                                    return member.id == "crew" &&
				                                                           member.name == "Navigator" &&
				                                                           !member.avatar.empty();
			                                                    });
		    });
		for (const auto &id : {"ship-two", "rival", "crew", "inspector"})
			require(host->lobby_command({LobbyAction::approve, id}), "Host approves an authenticated lobby member");
		pump(
		    [&]
		    {
			    return ship->lobby().members.size() == 5;
		    });
		pump(
		    [&]
		    {
			    const auto lobby = gm->lobby();
			    return std::any_of(lobby.members.begin(), lobby.members.end(),
			                       [](const auto &member)
			                       {
				                       return member.id == "crew" && member.name == "Navigator" &&
				                              !member.avatar.empty();
			                       });
		    });
		LobbyCommand reserve{LobbyAction::assign_local_console, "", "test.slot", "", "main"};
		reserve.screen = 2;
		require(host->lobby_command(reserve), "Native host reserves a console for a local display");
		const auto reserved = host->lobby();
		require(std::any_of(reserved.consoles.begin(), reserved.consoles.end(),
		                    [](const auto &seat)
		                    {
			                    return seat.slot == "test.slot" && seat.id == "main" && seat.local_screen == 2;
		                    }),
		        "Lobby roster reports local display reservations");
		reserve.screen = 3;
		require(host->lobby_command(reserve), "A console can move between local displays");
		require(host->set_local_player_profile(3, "Local navigator",
		                                       read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png")),
		        "Local console has an independent profile");
		pump(
		    [&]
		    {
			    for (const auto &m : gm->lobby().members)
				    if (m.local_screen == 3)
					    return m.name == "Local navigator" && !m.avatar.empty();
			    return false;
		    });
		require(ship->lobby_command({LobbyAction::claim_ship, "", "second", "test.ship"}),
		        "First ship requests a slot");
		require(rival->lobby_command({LobbyAction::claim_ship, "", "second", "test.ship"}),
		        "Competing ship requests the same slot");
		std::string winner;
		pump(
		    [&]
		    {
			    for (const auto &slot : host->lobby().slots)
				    if (slot.id == "second" && !slot.host.empty())
				    {
					    winner = slot.host;
					    return true;
				    }
			    return false;
		    });
		const auto loser = winner == "ship-two" ? "rival" : "ship-two";
		require(host->lobby_command({LobbyAction::leave, loser}), "Host can remove an unused competing connection");
		auto participating = winner == "ship-two" ? ship : rival;
		pump(
		    [&]
		    {
			    for (const auto &m : host->lobby().members)
				    if (m.id == winner)
					    return m.verified;
			    return false;
		    });
		require(crew->lobby_command({LobbyAction::select_console, "", "second", "", "main"}),
		        "Crew selects an available Console");
		require(gm->lobby_command({LobbyAction::select_console, "", "test.slot"}),
		        "Approved GM selects an inspection ship");
		pump(
		    [&]
		    {
			    for (const auto &m : host->lobby().members)
				    if (m.id == "crew")
					    return m.console == "main";
			    return false;
		    });
		pump(
		    [&]
		    {
			    for (const auto &m : host->lobby().members)
				    if (m.id == winner)
					    return m.verified;
			    return false;
		    });
		LobbyCommand local_ready{LobbyAction::ready};
		local_ready.screen = 3;
		require(host->lobby_command(local_ready), "Local player readies independently of the host");
		require(crew->lobby_command({LobbyAction::ready}), "Crew readies without requiring its ship host to ready");
		pump(
		    [&]
		    {
			    return host->lobby().countdown_ms >= 0;
		    });
		require(host->ticks() == 0 && participating->ticks() == 0,
		        "No simulation ticks occur during preparation/countdown");
		require(crew->lobby_command({LobbyAction::unready}), "Crew can cancel readiness");
		pump(
		    [&]
		    {
			    return host->lobby().countdown_ms < 0;
		    });
		require(!host->running() && !participating->running(), "Cancellation does not activate either server");
		LobbyCommand complexity{LobbyAction::select_profile, "", "second", "", "main"};
		complexity.profile = "automatic";
		require(!host->lobby_command(complexity), "Organiser cannot change another ship's occupied Console complexity");
		LobbyCommand unknown_profile{LobbyAction::select_profile, "", "test.slot", "", "main"};
		unknown_profile.profile = "missing";
		require(!host->lobby_command(unknown_profile), "Lobby rejects unknown authored complexity");
		require(crew->lobby_command({LobbyAction::ready}), "Crew can ready before changing complexity");
		pump(
		    [&]
		    {
			    return host->lobby().countdown_ms >= 0;
		    });
		require(crew->lobby_command(complexity), "Console owner chooses authored automatic complexity");
		pump(
		    [&]
		    {
			    const auto lobby = host->lobby();
			    return lobby.countdown_ms < 0 && std::any_of(
			                                         lobby.consoles.begin(), lobby.consoles.end(),
			                                         [](const auto &seat)
			                                         {
				                                         return seat.slot == "second" && seat.id == "main" &&
				                                                seat.profile == "automatic";
			                                         });
		    });
		require(!host->running(), "Complexity change invalidates readiness before simulation starts");
		pump(
		    [&]
		    {
			    for (const auto &member : host->lobby().members)
				    if (member.id == winner)
					    return member.verified;
			    return false;
		    });
		require(host->lobby_command(local_ready), "Local participant re-readies regenerated setup");
		crew->lobby_command({LobbyAction::ready});
		pump(
		    [&]
		    {
			    return host->running() && participating->running() && crew->observation().has_value() &&
			           !gm->inspection().is_null();
		    },
		    30000);
		pump(
		    [&]
		    {
			    return host->console_observation(3).has_value();
		    });
		require(!host->console_observation(2), "Moving a local console does not retain its old display claim");
		const auto selected_controllers = crew->observation()->gameplay.at("controllers");
		require(selected_controllers.size() == 1 && !selected_controllers[0].at("human").get<bool>(),
		        "Lobby complexity reaches launched World before human Console handover");
		require(host->release_console(3),
		        "Releasing a local display also releases its lobby reservation for late crew");
		require(
		    host->viewscreen().size() == 4,
		    "World contains its authored entity, two claimed ships and mandatory AI, with the optional slot absent");
		require(!host->observation(), "Readying an AI-only host must not claim a human Console");
		const auto saved = host->save();
		const auto restored = read_fleet_setup(saved);
		require(restored.plan.at("members").size() == 2 && restored.world->setup().ships.size() == 3,
		        "Portable save distinguishes server voters from deterministic AI-only ships");
		auto late_crew = join("late-crew", JoinRole::player);
		pump(
		    [&]
		    {
			    for (const auto &m : host->lobby().members)
				    if (m.id == "late-crew")
					    return !m.approved;
			    return false;
		    });
		const auto late_controls = host->lobby_controls();
		require(std::any_of(late_controls.begin(), late_controls.end(),
		                    [](const auto &control)
		                    {
			                    return control.first == "lobby.approve/late-crew";
		                    }),
		        "Running lobby UI retains approval controls after backend activation");
		require(host->session_action("lobby.approve/late-crew"),
		        "The running lobby admits late crew without changing the server roster");
		pump(
		    [&]
		    {
			    for (const auto &m : late_crew->lobby().members)
				    if (m.id == "late-crew")
					    return m.approved;
			    return false;
		    });
		require(late_crew->lobby_command({LobbyAction::select_console, "", "test.slot", "", "main"}),
		        "Late crew can take an available AI-covered Console");
		pump(
		    [&]
		    {
			    return late_crew->observation().has_value();
		    });
		auto late = join("late-ship", JoinRole::ship_server);
		pump(
		    [&]
		    {
			    return late->connection_status().find("cannot admit") != std::string::npos;
		    });
		// Lobby handoff approval binds the role as well as the identity and secret.
		auto guarded_options = options;
		guarded_options.link.room = "role-" + random_token();
		guarded_options.link.self = "guarded";
		const auto gm_ticket = random_token();
		guarded_options.lobby_tickets["wrong-role"] = {gm_ticket, JoinRole::gm};
		auto guarded = make_network_host(guarded_options);
		guarded->add_pack(write_pack(pack));
		guarded->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
		nodes.push_back(guarded);
		pump(
		    [&]
		    {
			    if (guarded->console_observation(9))
				    return true;
			    for (const auto &seat : guarded->console_seats())
				    if (seat.available)
					    guarded->claim_console(9, seat.address);
			    return false;
		    });
		const auto publication_observation = guarded->console_observation(9);
		// A continuously dragged control keeps commands queued across durable
		// writes. Both its admitted values and simulation time must still advance.
		ConsoleModel held_model;
		held_model.refresh(guarded, 9);
		const auto held_binding = held_model.widget_system("engine");
		const auto held_start = guarded->ticks();
		unsigned held_inputs = 0;
		auto keep_dragging = [&]
		{
			for (int i = 0; i < 4; ++i)
				held_model.set_speed_fraction(.05 + (++held_inputs % 20) * .001, held_binding);
		};
		keep_dragging();
		pump(
		    [&]
		    {
			    keep_dragging();
			    const auto view = guarded->console_observation(9);
			    return guarded->ticks() >= held_start + 5 && view &&
			           view->gameplay.at("controllers")[0].at("commands").at("speed").at("value") > 0 &&
			           (view->own.x != publication_observation->own.x || view->own.y != publication_observation->own.y);
		    },
		    5000);
		const auto &publication_controls = publication_observation->gameplay.at("controls");
		require(std::any_of(publication_controls.begin(), publication_controls.end(),
		                    [](const auto &control)
		                    {
			                    return control.value("action", "") == "publish.engine-enabled";
		                    }),
		        "explicit Console offers its permitted publication descriptor");
		OperatingCommand malformed_publication{publication_observation->own.id, "publish.engine-enabled", 0, {}};
		malformed_publication.controller = "propulsion";
		require(!guarded->console_command(9, malformed_publication), "publication refuses forged Controller envelope");
		require(guarded->console_command(9, {publication_observation->own.id, "publish.engine-enabled", 0, {}}),
		        "synthetic host explicitly publishes its permitted live record");
		pump(
		    [&]
		    {
			    return !guarded->public_board().value("shared", Json::array()).empty();
		    });
		require(guarded->release_console(9), "public-board fixture relinquishes publishing Console");
		auto attacking_options = guarded_options;
		attacking_options.server = "guarded";
		attacking_options.link.self = "wrong-role";
		attacking_options.inspector = false;
		attacking_options.admission = gm_ticket;
		attacking_options.lobby_tickets.clear();
		auto wrong_role = make_network_client(attacking_options);
		nodes.push_back(wrong_role);
		pump(
		    [&]
		    {
			    for (const auto &[id, text] : guarded->lobby_controls())
				    if (id == "network.approve/wrong-role")
					    return true;
			    return false;
		    });
		const auto role_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
		pump(
		    [&]
		    {
			    return std::chrono::steady_clock::now() >= role_deadline;
		    });
		require(!wrong_role->observation() && wrong_role->catalog().ordered_packs.empty(),
		        "A GM handoff credential cannot acquire crew content or command authority");
		auto direct_options = guarded_options;
		direct_options.server = "guarded";
		direct_options.link.self = "menu-handshake";
		direct_options.profile = "native-form";
		direct_options.lobby_tickets.clear();
		auto direct = join_client_lobby(direct_options, JoinRole::player);
		nodes.push_back(direct);
		std::string approve_direct;
		pump(
		    [&]
		    {
			    for (const auto &[id, text] : guarded->lobby_controls())
				    if (id.starts_with("network.approve/") && id != "network.approve/wrong-role")
				    {
					    approve_direct = id;
					    return true;
				    }
			    return false;
		    });
		require(approve_direct != "network.approve/menu-handshake",
		        "Direct-server discovery must not create an extra approval request");
		require(guarded->session_action(approve_direct), "The direct host approves the real native client identity");
		pump(
		    [&]
		    {
			    for (const auto &[id, text] : direct->lobby_controls())
				    if (id == "network.claim")
					    return true;
			    return false;
		    });
		pump(
		    [&]
		    {
			    return !direct->public_board().value("shared", Json::array()).empty();
		    });
		require(!direct->observation(), "approved remote Viewscreen receives public board without Console authority");
		require(direct->public_board().at("shared")[0].at("schema") == "test.public" &&
		            !direct->public_board().contains("records") && !direct->public_board().contains("controls"),
		        "unclaimed remote public projection carries only published evidence");
		require(direct->session_action("network.claim"),
		        "The native join form retains direct ship-server Console admission");
		pump(
		    [&]
		    {
			    return direct->observation().has_value();
		    });
		ConsoleModel remote_model;
		remote_model.refresh(direct);
		const auto remote_throttle = remote_model.widget_system("engine");
		require(remote_model.set_speed_fraction(0.333, remote_throttle),
		        "remote authored widget admits an arbitrary value outside generated step buttons");
		pump(
		    [&]
		    {
			    const auto view = direct->observation();
			    return view && view->gameplay.at("controllers")[0].at("commands").at("speed").at("value") == 5661;
		    });
		const auto remote_view = direct->observation();
		for (const auto &system : remote_view->systems)
			if (system.id == "engine" || system.id == "reserve")
				require(system.requested == 5661, "remote shared throttle drives both physical consumers");
		pump(
		    [&]
		    {
			    return !direct->observation()->gameplay.at("messages").empty();
		    });
		require(direct->observation()->gameplay.at("messages").size() == 1,
		        "human-operated remote propulsion receives one factual threshold report");
		OperatingCommand invalid_remote{remote_view->own.id, "speed_mm_s", 17001, {}};
		invalid_remote.controller = "propulsion";
		invalid_remote.channel = "speed";
		invalid_remote.generation = remote_view->gameplay.at("controllers")[0].at("generation");
		require(!direct->command(invalid_remote), "remote client refuses out-of-range arbitrary widget command");
		invalid_remote.value = 5678;
		invalid_remote.controller = "unknown";
		require(!direct->command(invalid_remote), "remote client refuses foreign widget address");
		remote_model.refresh(direct);
		bool remote_delegated = false;
		for (const auto &[id, label] : remote_model.gameplay_controls())
			if (label == "automatic")
				remote_delegated = remote_model.act(id);
		require(remote_delegated, "remote settings change Console complexity");
		pump(
		    [&]
		    {
			    return !direct->observation()->gameplay.at("controllers")[0].at("human").get<bool>();
		    });
		remote_model.refresh(direct);
		require(!remote_model.set_speed_fraction(0.5, remote_throttle), "remote AI-owned widget refuses human writes");
		require(direct->observation()->gameplay.at("messages").size() == 1,
		        "handover does not replay unchanged reporter warning");
		std::cout << "PASS authenticated slot arbitration, automatic setup transfer, per-player readiness, "
		             "cancellation, durable activation, AI-only slots and portable recovery\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
