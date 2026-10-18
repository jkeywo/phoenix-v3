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
import phoenix.network.host;
import phoenix.network.client;
import phoenix.session.local;
import phoenix.fleet_setup;
using namespace phoenix;
void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
int main(int argc, char **argv)
{
	try
	{
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
			auto node = join_game_lobby(config, role);
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
			    return host->lobby().members.size() == 5;
		    });
		for (const auto &id : {"ship-two", "rival", "crew", "inspector"})
			require(host->lobby_command({LobbyAction::approve, id}), "Host approves an authenticated lobby member");
		pump(
		    [&]
		    {
			    return ship->lobby().members.size() == 5;
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
		require(host->lobby_command({LobbyAction::ready}), "Empty-crew host can ready an AI ship");
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
		crew->lobby_command({LobbyAction::ready});
		pump(
		    [&]
		    {
			    return host->running() && participating->running() && crew->observation().has_value() &&
			           !gm->inspection().is_null();
		    },
		    30000);
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
		require(host->lobby_command({LobbyAction::approve, "late-crew"}),
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
		auto direct = join_game_lobby(direct_options, JoinRole::player);
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
		require(direct->session_action("network.claim"),
		        "The native join form retains direct ship-server Console admission");
		pump(
		    [&]
		    {
			    return direct->observation().has_value();
		    });
		std::cout << "PASS authenticated slot arbitration, automatic setup transfer, per-player readiness, "
		             "cancellation, durable activation, AI-only slots and portable recovery\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
