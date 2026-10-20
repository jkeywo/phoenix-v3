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
		defs["consoles"].push_back({{"id", "test.navigation"},
		                            {"human_seeking", true},
		                            {"widgets", Json::array({{{"id", "map"},
		                                                      {"kind", "navigation"},
		                                                      {"label", "Navigation"},
		                                                      {"binding", "ship.navigation"},
		                                                      {"controller", "planner"},
		                                                      {"channel", "point"},
		                                                      {"x", 0},
		                                                      {"y", 0},
		                                                      {"width", 1000},
		                                                      {"height", 1000}}})}});
		public_ship["stations"][0]["consoles"].push_back(
		    {{"id", "navigation"}, {"definition", "test.navigation"}, {"systems", Json::array()}});
		public_ship["control_layout"]["controllers"].push_back(
		    {{"id", "planner"},
		     {"console", "navigation"},
		     {"fallback", "hold"},
		     {"commands", Json::array({{{"id", "point"},
		                                {"action", "waypoint"},
		                                {"initial", 0},
		                                {"arguments", {0, 0}},
		                                {"consumers", Json::array()}}})}});
		for (auto &profile : public_ship["control_layout"]["profiles"])
			profile["consoles"]["navigation"] = {{"human", {"planner"}}};
		pack.files["definitions.json"] = json_bytes(defs);
		pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		{
			// A new content selection may reuse ship IDs with different authored
			// profiles. Retained lobby metadata must follow the new content.
			auto content_lobby = make_game_lobby(options, true);
			const GameSetup setup{"test.map", {{"test.slot", "test.ship"}}, "test.slot"};
			content_lobby->add_pack(write_pack(pack));
			content_lobby->prepare_lobby(setup);
			const auto main_seat = [](const LobbyProjection &projection)
			{
				for (const auto &seat : projection.consoles)
					if (seat.slot == "test.slot" && seat.id == "main")
						return seat;
				throw std::runtime_error("Synthetic lobby main Console is missing");
			};
			require(main_seat(content_lobby->lobby()).profile == "direct",
			        "Lobby exposes the original authored default profile");
			content_lobby->clear_packs();
			auto revised = defs;
			auto &layout = revised["entities"][0]["control_layout"];
			layout["profiles"][0]["id"] = "revised";
			layout["default_profile"] = "revised";
			auto replacement = pack;
			replacement.files["definitions.json"] = json_bytes(revised);
			content_lobby->replace_packs({write_pack(replacement)});
			content_lobby->prepare_lobby(setup);
			const auto seat = main_seat(content_lobby->lobby());
			require(seat.profile == "revised" &&
			            std::find(seat.profiles.begin(), seat.profiles.end(), "direct") == seat.profiles.end(),
			        "Reusing a ship ID after content replacement exposes current profile choices and default");
			LobbyCommand select{LobbyAction::select_profile, "", "test.slot", "", "main"};
			select.profile = "automatic";
			require(content_lobby->lobby_command(select) && main_seat(content_lobby->lobby()).profile == "automatic",
			        "Retained authored metadata does not freeze the selected lobby profile");
			select.profile = "direct";
			require(!content_lobby->lobby_command(select), "A removed authored profile cannot be selected");
		}
		{
			auto rooted = pack;
			rooted.manifest["base"] = {{"render_mode", "sprites"}};
			const auto archive = write_pack(rooted);
			auto config = options;
			config.link.room = "root-" + random_token();
			config.link.self = "root-host";
			config.server = "root-host";
			config.expected_base = base_configuration(read_pack(archive));
			auto root_host = make_game_lobby(config);
			root_host->add_pack(archive);
			root_host->prepare_lobby({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
			auto join_root = [&](std::string id, BaseConfiguration base)
			{
				auto client = config;
				client.link.self = id;
				client.profile = id;
				client.expected_base = std::move(base);
				return join_client_lobby(client, JoinRole::player);
			};
			auto wrong_id = config.expected_base;
			wrong_id.id = "another-base";
			auto wrong_hash = config.expected_base;
			wrong_hash.hash = std::string(64, '0');
			auto foreign = join_root("root-foreign", wrong_id), revision = join_root("root-revision", wrong_hash),
			     matching = join_root("root-match", config.expected_base);
			bool complete = false;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
			while (std::chrono::steady_clock::now() < deadline)
			{
				for (const auto &node : {root_host, foreign, revision, matching})
					node->poll();
				const auto members = root_host->lobby().members;
				const auto refused = [](const auto &node)
				{
					return node->lobby().status.find("same base pack revision") != std::string::npos;
				};
				if (refused(foreign) && refused(revision) &&
				    std::any_of(members.begin(), members.end(),
				                [](const auto &m)
				                {
					                return m.id == "root-match";
				                }))
				{
					require(std::none_of(members.begin(), members.end(),
					                     [](const auto &m)
					                     {
						                     return m.id == "root-foreign" || m.id == "root-revision";
					                     }),
					        "Incompatible launch roots are refused before lobby admission");
					complete = true;
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			require(complete, "Real network accepts matching launch roots and refuses foreign IDs/revisions");
		}
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
		const auto unclaimed_board = direct->public_board();
		require(std::any_of(unclaimed_board.at("shared").begin(), unclaimed_board.at("shared").end(),
		                    [](const auto &row)
		                    {
			                    return row.at("schema") == "test.public";
		                    }) &&
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
		require(remote_model.on_screen("radar", {0, 0, 1}), "remote source admits On screen through verified widget");
		pump(
		    [&]
		    {
			    return direct->public_board().contains("display");
		    });
		require(guarded->public_board().at("display").at("kind") == "radar" &&
		            direct->public_board().at("display").at("revision") ==
		                guarded->public_board().at("display").at("revision"),
		        "remote command reaches host and live read-only display returns to viewer");
		require(remote_model.act("game.console/navigation") && remote_model.on_screen("map", {50000, 0, 2000}),
		        "remote supplemental navigation publishes with its selected Console address");
		pump(
		    [&]
		    {
			    return direct->public_board().contains("display") &&
			           direct->public_board().at("display").at("kind") == "navigation";
		    });
		require(guarded->public_board().at("display").at("console") == "navigation",
		        "host consensus frame retains supplemental Console identity");
		require(remote_model.on_screen("map", {70000, 0, 1000}, true),
		        "remote active navigation admits live pan and zoom");
		pump(
		    [&]
		    {
			    return direct->public_board().at("display").at("camera") == Json::array({70000, 0, 1000});
		    });
		require(remote_model.on_screen("map", {70000, 0, 1000}), "remote source admits second press");

		pump(
		    [&]
		    {
			    return !direct->public_board().contains("display");
		    });
		require(remote_model.act("game.console/main"), "remote operator returns to primary Console");
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
		{
			auto gm_options = options;
			gm_options.link.room = "gm-" + random_token();
			gm_options.link.self = "gm-host";
			gm_options.profile = "gm-host";
			auto organiser = make_game_lobby(gm_options);
			organiser->add_pack(write_pack(pack));
			GameSetup setup{"test.map"};
			setup.gm_host = true;
			organiser->prepare_lobby(setup);
			nodes.push_back(organiser);
			const auto lobby = organiser->lobby();
			require(lobby.active && !lobby.launched && lobby.members.front().role == JoinRole::gm &&
			            lobby.members.front().slot.empty() &&
			            std::all_of(lobby.slots.begin(), lobby.slots.end(),
			                        [](const auto &slot)
			                        {
				                        return slot.host.empty() && !slot.ship.empty();
			                        }),
			        "A GM lobby leaves every ship slot free and supplies default AI ships");
			require(organiser->session_action("lobby.ready"), "The GM has an explicit Ready action");
			pump(
			    [&]
			    {
				    return organiser->running();
			    });
			require(!organiser->inspection().is_null() && !organiser->observation() &&
			            organiser->console_seats().empty(),
			        "Solo GM launch exposes inspection without claiming a ship or player Console");

			auto mixed_options = gm_options;
			mixed_options.link.room = "mixed-" + random_token();
			mixed_options.profile = "mixed-gm";
			auto mixed = make_game_lobby(mixed_options);
			mixed->add_pack(write_pack(pack));
			mixed->prepare_lobby(setup);
			nodes.push_back(mixed);
			auto crew_options = mixed_options;
			crew_options.server = mixed_options.link.self;
			crew_options.link.self = "joined-ship";
			crew_options.profile = "joined-ship";
			auto joined = join_game_lobby(crew_options, JoinRole::ship_server);
			nodes.push_back(joined);
			pump(
			    [&]
			    {
				    return mixed->lobby().members.size() == 2;
			    });
			require(mixed->session_action("lobby.approve/joined-ship"), "GM approves a joining ship host");
			pump(
			    [&]
			    {
				    return !joined->catalog().maps.empty();
			    });
			require(joined->session_action("lobby.ship/test.slot/test.ship"),
			        "Ship host can take a slot in a GM lobby");
			pump(
			    [&]
			    {
				    for (const auto &member : joined->lobby().members)
					    if (member.id == "joined-ship")
						    return member.verified && member.slot == "test.slot";
				    return false;
			    });
			require(mixed->session_action("lobby.ready") && joined->session_action("lobby.ready"),
			        "GM and joining ship host ready independently");
			pump(
			    [&]
			    {
				    return mixed->running() && joined->running();
			    });
			require(mixed->console_seats().empty() && !joined->console_seats().empty(),
			        "Mixed GM fleet preserves exclusive ship ownership");
		}
		{
			auto same = options;
			same.link.room = "same-machine-" + random_token();
			same.link.self = "organiser";
			auto shared_host = make_game_lobby(same);
			shared_host->add_pack(write_pack(pack));
			shared_host->prepare_lobby({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
			same.server = "organiser";
			same.link.self.clear();
			same.profile = "default";
			auto first = join_client_lobby(same, JoinRole::player);
			auto second = join_client_lobby(same, JoinRole::player);
			nodes = {shared_host, first, second};
			pump(
			    [&]
			    {
				    return shared_host->lobby().members.size() == 3;
			    });
			const auto first_id = first->lobby().self, second_id = second->lobby().self;
			require(!first_id.empty() && !second_id.empty() && first_id != second_id,
			        "Two clients on one machine with the same default profile receive independent identities");
			require(shared_host->lobby_command({LobbyAction::approve, first_id}) &&
			            shared_host->lobby_command({LobbyAction::approve, second_id}),
			        "Both same-profile clients can be approved together");
			pump(
			    [&]
			    {
				    const auto members = shared_host->lobby().members;
				    return first->lobby().members.size() == 3 && second->lobby().members.size() == 3 &&
				           std::count_if(members.begin(), members.end(),
				                         [](const auto &member)
				                         {
					                         return member.connected && member.approved;
				                         }) == 3;
			    });
		}
		std::cout << "PASS authenticated slot arbitration, automatic setup transfer, per-player readiness, "
		             "cancellation, durable activation, AI-only slots and portable recovery\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
