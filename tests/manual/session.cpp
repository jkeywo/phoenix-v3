#include <chrono>
#include <iostream>
#include <thread>
#include <array>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

import phoenix.network.client;
import phoenix.network.host;
import phoenix.session.api;
import phoenix.wire;
import phoenix.fleet_setup;

using namespace phoenix;
int fleet_journey(NetworkOptions options)
{
	auto require = [](bool value, const char *message)
	{
		if (!value)
			throw std::runtime_error(message);
	};
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", "test-core"}};
	auto definitions =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic multi-Console fleet");
	definitions["schema"] = 2;
	definitions["entities"][0].erase("console");
	definitions["consoles"].push_back({{"id", "test.empty"}, {"widgets", Json::array()}});
	definitions["entities"][0]["stations"] = Json::array(
	    {{{"id", "bridge"},
	      {"consoles", Json::array({{{"id", "helm"}, {"definition", "test.empty"}, {"systems", {"engine", "steering"}}},
	                                {{"id", "sensors"}, {"definition", "test.empty"}, {"systems", {"radar"}}}})}}});
	auto original_slot = definitions["maps"][0]["ship_slots"][0];
	for (int i = 2; i <= 3; ++i)
	{
		auto slot = original_slot;
		slot["id"] = "slot" + std::to_string(i);
		slot["x_mm"] = 50000 * i;
		definitions["maps"][0]["ship_slots"].push_back(slot);
	}
	pack.files["definitions.json"] = json_bytes(definitions);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	auto source = make_host_world();
	source->add_pack(write_pack(pack));
	const auto bundle =
	    export_fleet_setup(*source, {"test.map", {{"test.slot", "test.ship"}}, "test.slot", "helm"}, options.link.room);
	std::array<std::shared_ptr<SessionPort>, 3> hosts;
	auto make = [&](int i)
	{
		auto config = options;
		config.link.self = "initial" + std::to_string(i);
		config.profile = "ship" + std::to_string(i + 1);
		auto host = make_network_host(config);
		host->restore(bundle);
		require(host->session_action("network.member/ship" + std::to_string(i + 1)),
		        "Cannot select prepared fleet member");
		host->resume_local();
		return host;
	};
	for (int i = 0; i < 3; ++i)
		hosts[i] = make(i);
	auto config = options;
	config.link.self = "crew";
	config.server = "ship1-console";
	config.profile = "crew";
	auto client = make_network_client(config);
	bool advance = true;
	auto next_tick = std::chrono::steady_clock::now();
	auto poll = [&]
	{
		for (auto &host : hosts)
			if (host)
				host->poll();
		client->poll();
		const auto now = std::chrono::steady_clock::now();
		if (advance && now >= next_tick)
		{
			for (auto &host : hosts)
				if (host)
					host->tick();
			next_tick = now + std::chrono::milliseconds(50);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	};
	auto until = [&](const std::function<bool()> &done)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		while (!done())
		{
			if (std::chrono::steady_clock::now() > deadline)
				throw std::runtime_error("Playable fleet timeout: " + hosts[0]->connection_status() + "; " +
				                         client->connection_status());
			poll();
		}
	};
	until(
	    [&]
	    {
		    return hosts[0]->ticks() >= 3 && hosts[0]->observation() && hosts[1]->observation() &&
		           hosts[2]->observation();
	    });
	require(client->catalog().ordered_packs.empty() && !client->observation(),
	        "Unapproved crew received content or authority");
	until(
	    [&]
	    {
		    for (const auto &[id, text] : hosts[0]->lobby_controls())
			    if (id.starts_with("network.approve/"))
				    hosts[0]->session_action(id);
		    return client->console_seats().size() == 2;
	    });
	const auto seats = client->console_seats();
	for (const auto &seat : seats)
		if (seat.address.console == "sensors")
			require(client->claim_console(1, seat.address), "Remote sensors claim was not queued");
	until(
	    [&]
	    {
		    return client->observation().has_value();
	    });
	auto ship = hosts[0]->observation()->own.id;
	require(client->observation()->console == "sensors" && hosts[0]->observation()->console == "helm",
	        "Mixed local/remote Consoles collided");
	require(hosts[0]->command({ship, "speed_mm_s", 2000}) && client->command({ship, "radar_enabled", 0}),
	        "Independent fleet Console commands were refused");
	require(!hosts[0]->command({ship, "radar_enabled", 0}), "Local helm bypassed its System control boundary");
	until(
	    [&]
	    {
		    return hosts[0]->observation()->requested_speed == 2000 && !client->observation()->radar_enabled;
	    });
	const auto before = hosts[0]->ticks();
	hosts[2].reset();
	until(
	    [&]
	    {
		    return hosts[0]->ticks() > before + 10;
	    });
	hosts[2] = make(2);
	until(
	    [&]
	    {
		    return hosts[2]->ticks() >= hosts[0]->ticks() && hosts[2]->observation();
	    });
	advance = false;
	until(
	    [&]
	    {
		    const auto a = hosts[0]->inspection();
		    return !a.is_null() && a == hosts[1]->inspection() && a == hosts[2]->inspection();
	    });
	const auto saved_tick = hosts[0]->ticks();
	auto portable = hosts[0]->save();
	auto recovered = read_fleet_setup(portable, "ship2");
	auto initial = read_fleet_setup(bundle);
	require(recovered.plan.at("session") != initial.plan.at("session") &&
	            recovered.plan.at("key") != initial.plan.at("key"),
	        "Portable fleet recovery reused credentials");
	require(recovered.world->ticks() == saved_tick && !recovered.world->observation(),
	        "Portable fleet recovery changed tick or retained a live operator");
	std::cout << "PASS playable fleet local/remote Consoles, approval, majority continuation, durable return and "
	             "portable recovery"
	          << std::endl;
	return 0;
}
int main(int argc, char **argv)
{
	NetworkOptions options;
	options.link.url = "ws://127.0.0.1:9080/phoenix";
	options.link.room = "synthetic-session";
	options.server = "host";
	options.data_location = "build/session-probe";
	bool host = false, confirm = false, blocked = false, approve = false, fleet = false;
	unsigned seconds = 20;
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "--host")
			host = true;
		else if (arg == "--fleet-journey")
			fleet = true;
		else if (arg == "--gm")
			options.inspector = true;
		else if (arg == "--relay")
			options.link.force_relay = true;
		else if (arg == "--confirm")
			confirm = true;
		else if (arg == "--blocked")
			blocked = true;
		else if (arg == "--approve-gm")
			approve = true;
		else if (i + 1 < argc)
		{
			std::string value = argv[++i];
			if (arg == "--url")
				options.link.url = value;
			if (arg == "--room")
				options.link.room = value;
			if (arg == "--self")
				options.link.self = value;
			if (arg == "--server")
				options.server = value;
			if (arg == "--data")
				options.data_location = value;
			if (arg == "--profile")
				options.profile = value;
			if (arg == "--seconds")
				seconds = static_cast<unsigned>(std::stoul(value));
		}
	}
	try
	{
		if (fleet)
			return fleet_journey(options);
		if (options.link.self.empty())
			options.link.self = host ? options.server : options.inspector ? "test-gm" : "test-client";
		auto session = host ? make_network_host(options) : make_network_client(options);
		if (host)
		{
			PackDraft pack;
			pack.manifest = {{"schema", 1}, {"id", "test-core"}};
			auto definitions =
			    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic fixture");
			definitions["maps"][0]["entities"].push_back(
			    {{"id", 2}, {"definition", "test.variant"}, {"x_mm", 200000}, {"y_mm", 0}, {"heading_mdeg", 0}});
			pack.files["definitions.json"] = json_bytes(definitions);
			pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
			session->add_pack(write_pack(pack));
			session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
		}
		const auto start = std::chrono::steady_clock::now(), deadline = start + std::chrono::seconds(seconds);
		auto next_tick = start;
		bool sent = false, saw_unapproved = false, claim_requested = false, confirmation_requested = false;
		std::int64_t x = 0, y = 0;
		std::string previous;
		std::cout << (host ? "HOST_READY " : "CLIENT_START ") << session->invitation_code() << std::endl;
		while (std::chrono::steady_clock::now() < deadline)
		{
			session->poll();
			const auto now = std::chrono::steady_clock::now();
			if (now >= next_tick)
			{
				session->tick();
				next_tick = now + std::chrono::milliseconds(50);
			}
			auto controls = session->lobby_controls();
			if (host)
			{
				if (now - start > std::chrono::milliseconds(500))
					for (const auto &[id, label] : controls)
						if (id.starts_with("network.approve/"))
							session->session_action(id);
				if (approve && now - start > std::chrono::milliseconds(500))
					for (const auto &[id, label] : controls)
						if (id.rfind("network.gm/", 0) == 0)
							session->session_action(id);
			}
			else if (options.inspector)
			{
				if (session->inspection().is_null())
					saw_unapproved = true;
				if (!session->inspection().is_null())
				{
					if (!saw_unapproved || session->command({3, "speed_mm_s", 1000}) ||
					    session->inspection().at("bodies").size() != 3)
						throw std::runtime_error("GM approval/read-only/full-world boundary failed");
					std::cout << "PASS host-approved read-only GM inspection" << std::endl;
					return 0;
				}
			}
			else
			{
				if (!session->catalog().ordered_packs.empty() &&
				    (!session->catalog().presentation || !session->catalog().maps.empty() ||
				     !session->catalog().trees.empty() || session->catalog().entities.size() != 1))
					throw std::runtime_error(
					    "Crew received authoritative content instead of permitted presentation assets");
				if (!session->observation())
					for (const auto &[id, label] : controls)
					{
						if (blocked && id == "network.claim")
						{
							if (session->command({3, "speed_mm_s", 1000}))
								throw std::runtime_error("Unclaimed client admitted an operating request");
							std::cout << "PASS unclaimed/previous operator has no control projection" << std::endl;
							return 0;
						}
						if (id == "network.confirm")
						{
							if (!confirm)
							{
								std::cout << "NEEDS_CONFIRM disconnected station replacement" << std::endl;
								return 0;
							}
							if (!confirmation_requested)
							{
								session->session_action(id);
								confirmation_requested = true;
							}
						}
						else if (id == "network.claim" && !claim_requested)
						{
							session->session_action(id);
							claim_requested = true;
						}
					}
				if (auto view = session->observation())
				{
					if (!sent)
					{
						if (session->command({2, "speed_mm_s", 1000}))
							throw std::runtime_error("Client admitted a command for another ship");
						for (const auto &contact : view->contacts)
							if (contact.id == 2)
								throw std::runtime_error("Remote radar leaked an out-of-range entity");
						x = view->own.x;
						y = view->own.y;
						if (session->command({view->own.id, "speed_mm_s", 1000}) &&
						    session->command({view->own.id, "heading_mdeg", (view->own.heading + 90000) % 360000}))
							sent = true;
					}
					else if (view->own.x != x && view->own.y != y)
					{
						std::cout << "PASS remote verified content, committed claim, scoped radar, engine and steering"
						          << std::endl;
						return 0;
					}
				}
			}
			const auto status = session->connection_status();
			if (status != previous)
			{
				std::cout << status << std::endl;
				previous = status;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (host)
			return 0;
		throw std::runtime_error("Session integration timed out: " + session->connection_status());
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << std::endl;
		return 1;
	}
}
