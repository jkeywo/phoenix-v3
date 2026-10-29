#include "../support/fixtures.hpp"
#include <chrono>
#include <cstddef>
#include <algorithm>
#include <iostream>
#include <stdexcept>
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
#include <tuple>

import phoenix.network.client;
import phoenix.network.host;
import phoenix.session.api;
import phoenix.wire;
import phoenix.fleet_setup;
import phoenix.persistence;

using namespace phoenix;
// Kept in the existing live-session probe: this is permitted authored data
// crossing the real chunked projection, not a separate mock transport fixture.
void large_network_knowledge(Json &definitions)
{
	definitions["schema"] = 4;
	for (const auto &[kind, binding] : std::vector<std::pair<std::string, std::string>>{
	         {"camera_mode", "ship.camera.mode"}, {"camera_direction", "ship.camera.direction"}})
		definitions["consoles"][0]["widgets"].push_back({{"id", kind},
		                                                 {"kind", kind},
		                                                 {"binding", binding},
		                                                 {"label", kind},
		                                                 {"x", 0},
		                                                 {"y", 0},
		                                                 {"width", 300},
		                                                 {"height", 300}});
	definitions["knowledge"] = Json::parse(R"json({
		"history_limit":8,"roles":["crew"],"schemas":[
		 {"id":"network-speed","producers":["crew"],"readers":["crew"],"ship_publishable":true,
		  "fields":[{"id":"requested","type":"integer","publishable":true}]},
		 {"id":"network-report","producers":["crew"],"readers":["crew"],"fields":[]}
		]
	})json");
	Json payload = Json::object();
	for (int i = 0; i < 12; ++i)
	{
		const auto field = "part" + std::to_string(i);
		definitions["knowledge"]["schemas"][1]["fields"].push_back({{"id", field}, {"type", "text"}});
		payload[field] = std::string(4096, static_cast<char>('a' + i));
	}
	definitions["consoles"][0]["knowledge_roles"] = {"crew"};
	definitions["consoles"][0]["knowledge_producers"] = {"crew"};
	auto &ship = definitions["entities"][0];
	ship["systems"]["engine"]["records"] =
	    Json::array({{{"schema", "network-speed"}, {"producer", "crew"}, {"fields", {{"requested", "requested"}}}}});
	ship["publications"] = Json::array({{{"id", "network-speed"},
	                                     {"source_console", "main"},
	                                     {"schema", "network-speed"},
	                                     {"subject", 0},
	                                     {"fields", {"requested"}},
	                                     {"live", true}}});
	ship["message_templates"] = {"test.network-report"};
	definitions["messages"] = Json::array({{{"id", "test.network-report"},
	                                        {"schema", "network-report"},
	                                        {"title_key", "test.network.report"},
	                                        {"body_key", "test.network.body"},
	                                        {"destination", {{"kind", "console"}, {"id", "main"}}},
	                                        {"delay_ticks", 1},
	                                        {"fields", payload}}});
}
int fleet_journey(NetworkOptions options)
{
	// Exercise automatic compaction through the real host and flushed journal
	// without requiring thousands of durable writes within a wall-clock budget.
	constexpr std::uint64_t checkpoint_frames = 64;
	options.checkpoint_frames = checkpoint_frames;
	auto require = [](bool value, const char *message)
	{
		if (!value)
			throw std::runtime_error(message);
	};
	auto require_value = [](const std::string &name, std::int64_t actual, std::int64_t expected)
	{
		if (actual != expected)
			throw std::runtime_error(name + ": expected " + std::to_string(expected) + ", observed " +
			                         std::to_string(actual));
	};
	auto definitions =
	    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic multi-Console fleet");
	definitions["schema"] = 4;
	auto &systems = definitions["entities"][0]["systems"];
	systems["drive"] = systems["engine"];
	systems["reserve"] = systems["engine"];
	systems["drive"]["kind"] = systems["reserve"]["kind"] = "engine";
	for (const char *id : {"drive", "reserve"})
	{
		systems[id]["reverse_speed_mm_s"] = 5000;
		systems[id]["power_rate"] = 200;
		systems[id]["power_store"] = "cell";
	}
	systems["steering"]["power_rate"] = 20;
	systems["steering"]["power_store"] = "cell";
	systems["side"] = {{"kind", "lateral"},          {"max_speed_mm_s", 6000},
	                   {"lateral_speed_mm_s", 6000}, {"lateral_acceleration_mm_s2", 1000},
	                   {"power_rate", 100},          {"power_store", "cell"}};
	systems["storage"] = {{"kind", "storage"}, {"power_store", "cell"}};
	definitions["resources"] = Json::array({{{"id", "fixture.resource.energy"},
	                                         {"label", "Fleet energy"},
	                                         {"tags", {"energy"}},
	                                         {"sizes", {{"energy", 1}}}}});
	definitions["entities"][0]["resources"] = Json::parse(R"json({"stores":[
		{"id":"cell","resource":"fixture.resource.energy","capacity":2000000,"initial":1000000,"charge_rate":10000,
		 "discharge_rate":10000,"stop_heat":100000,"recover_heat":50000}
	]})json");
	systems.erase("engine");
	systems["search"] = systems["radar"];
	systems["search"]["kind"] = "radar";
	systems.erase("radar");
	definitions["entities"][0].erase("console");
	definitions["consoles"].push_back({{"id", "test.empty"}, {"widgets", Json::array()}});
	definitions["entities"][0]["stations"] = Json::array(
	    {{{"id", "bridge"},
	      {"consoles", Json::array({{{"id", "helm"}, {"definition", "test.empty"}, {"systems", {"drive", "steering"}}},
	                                {{"id", "sensors"},
	                                 {"definition", "test.empty"},
	                                 {"systems", {"reserve", "search", "side", "storage"}}}})}}});
	// One objective belongs to ship1's crew. Other fixed fleet members must not
	// receive it through the portable projection or while recovering a checkpoint.
	auto &map = definitions["maps"][0];
	map["regions"] = Json::array(
	    {{{"id", "destination"}, {"shape", "circle"}, {"x_mm", 100000000}, {"y_mm", 0}, {"radius_mm", 1000}}});
	map["scenario"] = Json::parse(R"json({
		"tree":"test.network-objective","bindings":{"actor":{"slot":"test.slot"}},
		"objectives":[{"id":"reach","type":"reach_region","region":"destination",
		 "actor":{"slot":"test.slot"},"recipients":{"ship":{"slot":"test.slot"}},
		 "title_key":"test.network.destination"}]
	})json");
	definitions["trees"] = Json::parse(R"json([
		{"id":"test.network-objective","context":"scenario","initial":"travelling","states":[
		 {"id":"travelling","entry":[{"action":"add_objective",
		  "arguments":["reach",{"observation":"self.entity"},"network-arrival"]}]}]},
		{"id":"test.storage-summary","context":"system","initial":"ready","states":[
		 {"id":"ready","running":[{"action":"set_storage_charge_limit",
		  "arguments":[{"observation":"self.system"},"cell",{"observation":"summary.level"}]}]}]}
	])json");
	auto &layout_ship = definitions["entities"][0];
	layout_ship["motion_model"] = "inertial";
	layout_ship["systems"]["reserve2"] = layout_ship["systems"]["reserve"];
	layout_ship["systems"]["reserve2"]["impulse"] = {{"charge_ticks", 20}};
	auto &console_rows = layout_ship["stations"][0]["consoles"];
	for (auto &console : console_rows)
	{
		console["feeds"] = console["systems"];
		console["systems"] = Json::array();
	}
	console_rows[1]["feeds"].push_back("reserve2");
	layout_ship["control_layout"] = Json::parse(R"json({"schema":1,"default_profile":"direct",
      "controllers":[
       {"id":"drive","console":"helm","fallback":"hold","commands":[{"id":"speed_mm_s","action":"speed_mm_s","maximum":15000,"consumers":["drive"]}]},
       {"id":"steering","console":"helm","fallback":"hold","commands":[{"id":"heading_mdeg","action":"heading_mdeg","consumers":["steering"]}]},
       {"id":"reserve","console":"sensors","fallback":"hold","commands":[
         {"id":"speed_mm_s","action":"speed_mm_s","maximum":15000,"consumers":["reserve","reserve2"]},
         {"id":"assisted","action":"assisted","initial":1,"consumers":["reserve","reserve2"]},
         {"id":"longitudinal_thrust","action":"longitudinal_thrust","consumers":["reserve","reserve2"]},
         {"id":"impulse","action":"impulse","consumers":["reserve2"]}]},
       {"id":"search","console":"sensors","fallback":"hold","commands":[{"id":"radar_enabled","action":"radar_enabled","initial":1,"consumers":["search"]}]},
       {"id":"side","console":"sensors","fallback":"hold","commands":[{"id":"lateral_thrust","action":"lateral_thrust","consumers":["side"]}]},
       {"id":"storage","console":"sensors","tree":"test.storage-summary","commands":[{"id":"charge_limit","action":"charge_limit","initial":400,"maximum":8000,"consumers":["storage"]}]},
       {"id":"preference","console":"sensors","fallback":"hold","commands":[{"id":"level","action":"intent_integer","initial":200,"minimum":0,"maximum":800,"consumers":[]}]}],
      "profiles":[
       {"id":"direct","consoles":{"helm":{"human":["drive","steering"]},"sensors":{"human":["reserve","search","side","storage","preference"]}}},
       {"id":"assisted","consoles":{"helm":{"human":["drive","steering"]},"sensors":{"human":["reserve","search","side","preference"],"ai":["storage"]}}}]
    })json");
	definitions["trees"][1] = Json::parse(R"json({"id":"test.storage-summary","context":"controller","initial":"ready",
       "command_bindings":[{"channel":"charge_limit"}],
       "command_reads":[{"name":"preference","controller":"preference","channel":"level"}],
       "states":[{"id":"ready","running":[{"action":"write.charge_limit","arguments":[{"observation":"preference"}]}]}]})json");
	definitions["knowledge"] = Json::parse(
	    R"json({"roles":["crew"],"schemas":[{"id":"test.fact","producers":["crew"],"readers":["crew"],"fields":[{"id":"amount","type":"integer"},{"id":"flag","type":"boolean"},{"id":"detail","type":"text"}]}]})json");
	definitions["consoles"][1]["knowledge_roles"] = {"crew"};
	definitions["consoles"][1]["knowledge_producers"] = {"crew"};
	layout_ship["message_templates"] = {"test.high-throttle"};
	definitions["messages"] = Json::parse(
	    R"json([{"id":"test.high-throttle","schema":"test.fact","destination":{"kind":"controller","console":"sensors","id":"reserve"},"title_key":"High throttle","body_key":"Requested speed exceeds the reporting threshold.","delay_ticks":1,"automatic":false,"fields":{"amount":{"argument":"amount"},"flag":{"argument":"flag"},"detail":{"argument":"detail"}}}])json");
	layout_ship["systems"]["reserve"]["reporters"] =
	    Json::parse(R"json([{"id":"high","field":"requested","above":500,"message":"test.high-throttle"}])json");
	// A separate safe layout exposes continuous controls on the remote Console.
	auto remote_layout = definitions["consoles"][1];
	remote_layout["id"] = "test.remote-controls";
	remote_layout["widgets"] = Json::parse(R"json([
	 {"id":"throttle","kind":"throttle","label":"Remote throttle","controller":"reserve","channel":"speed_mm_s",
	  "x":0,"y":0,"width":500,"height":1000},
	 {"id":"radar","kind":"radar","label":"Radar","binding":"radar.contacts",
	  "x":500,"y":0,"width":500,"height":1000}])json");
	definitions["consoles"].push_back(remote_layout);
	layout_ship["stations"][0]["consoles"][1]["definition"] = "test.remote-controls";
	auto original_slot = definitions["maps"][0]["ship_slots"][0];
	for (int i = 2; i <= 3; ++i)
	{
		auto slot = original_slot;
		slot["id"] = "slot" + std::to_string(i);
		slot["x_mm"] = 50000 * i;
		definitions["maps"][0]["ship_slots"].push_back(slot);
	}
	const auto pack = phoenix::test::fixture_pack(json_bytes(definitions), "test-core");
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
	bool advance = true, accelerated = false;
	std::string stage = "initial admission";
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
			next_tick = now + std::chrono::milliseconds(accelerated ? 0 : 50);
		}
		// Accelerated checkpoint coverage is driven by durable progress, not timer granularity.
		// A 1 ms sleep can consume a full scheduler quantum on a headless Windows runner.
		if (accelerated)
			std::this_thread::yield();
		else
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
	};
	auto until = [&](const std::function<bool()> &done)
	{
		std::cout << "FLEET_STAGE " << stage << " at tick " << hosts[0]->ticks() << std::endl;
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		auto progress = hosts[0]->ticks();
		while (!done())
		{
			// This is recovery verification, not a disk-throughput benchmark. The Node
			// harness bounds the whole journey; detect a stalled accelerated quorum here.
			if (accelerated && hosts[0]->ticks() > progress)
			{
				progress = hosts[0]->ticks();
				deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			}
			if (std::chrono::steady_clock::now() > deadline)
				throw std::runtime_error(
				    "Playable fleet timeout during " + stage + " at tick " + std::to_string(hosts[0]->ticks()) + ": " +
				    hosts[0]->connection_status() + "; " + client->connection_status() +
				    "; local=" + (hosts[0]->observation() ? hosts[0]->observation()->console : "none") +
				    "; remote=" + (client->observation() ? client->observation()->console : "none"));
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
	auto fitted = [](const Observation &view, const std::string &id)
	{
		const auto found = std::find_if(view.systems.begin(), view.systems.end(),
		                                [&](const auto &value)
		                                {
			                                return value.id == id;
		                                });
		if (found == view.systems.end())
			throw std::runtime_error("Network projection omitted fitted System " + id);
		return *found;
	};
	auto remote_value = [&](const std::string &id, const std::string &field)
	{
		return fitted(*client->observation(), id).telemetry.at(field);
	};
	auto objectives = [](const Observation &view)
	{
		Json records = Json::array();
		for (const char *scope : {"records", "shared"})
			for (const auto &record : view.gameplay.value(scope, Json::array()))
				if (record.value("schema", std::string{}) == "phoenix.objective")
					records.push_back(record);
		return records;
	};
	require(client->catalog().presentation && client->catalog().maps.empty() && client->catalog().trees.empty() &&
	            client->catalog().entities.size() == 1 && remote_value("reserve", "minimum_speed") == -5000 &&
	            remote_value("storage", "stored_energy") > 0,
	        "Schema-4 remote assets or signed flight/resource projection lost their permitted bounds");
	require(!objectives(*client->observation()).empty() && !objectives(*hosts[0]->observation()).empty() &&
	            objectives(*hosts[1]->observation()).empty() && objectives(*hosts[2]->observation()).empty(),
	        "Scenario objective projection lost its ship scope across real network sessions");
	const auto objective = objectives(*client->observation()).front();
	require(objective.contains("observed_tick") && objective.contains("uncertainty"),
	        "Remote objective knowledge lost its provenance");
	auto human = [&](const std::string &id)
	{
		const auto current_view = client->observation();
		for (const auto &owner : current_view->gameplay.at("controllers"))
			if (owner.at("id") == id)
				return owner.at("human").get<bool>();
		return false;
	};
	// Arbitrary continuous values and neutral release must cross the real adapter,
	// even when no generated step descriptor contains that value.
	auto continuous = [&](const std::string &owner, const std::string &channel, std::int64_t value)
	{
		OperatingCommand c{ship, channel, value};
		c.console = client->observation()->console;
		c.controller = owner;
		c.channel = channel;
		const auto current = client->observation();
		for (const auto &section : current->gameplay.at("controllers"))
			if (section.at("id") == owner)
				c.generation = section.at("generation");
		return c;
	};
	auto accepted_value = [&](const std::string &owner, const std::string &channel)
	{
		const auto current = client->observation();
		for (const auto &section : current->gameplay.at("controllers"))
			if (section.at("id") == owner)
				return section.at("commands").at(channel).at("value").get<std::int64_t>();
		throw std::runtime_error("Remote Controller missing");
	};
	auto arbitrary = continuous("reserve", "speed_mm_s", 123);
	require(client->command(arbitrary), "remote authored throttle admits a non-descriptor value");
	until(
	    [&]
	    {
		    return accepted_value("reserve", "speed_mm_s") == 123;
	    });
	auto stale_control = arbitrary;
	++stale_control.generation;
	auto out_of_range = arbitrary;
	out_of_range.value = 15001;
	require(!client->command(stale_control) && !client->command(out_of_range),
	        "remote numeric controls refuse stale authority and values outside the authored range");
	for (const auto &[owner, action, value] :
	     std::vector<std::tuple<std::string, std::string, std::int64_t>>{{"reserve", "assisted", 0},
	                                                                     {"reserve", "longitudinal_thrust", 417},
	                                                                     {"reserve", "longitudinal_thrust", 0},
	                                                                     {"side", "lateral_thrust", -417},
	                                                                     {"side", "lateral_thrust", 0},
	                                                                     {"reserve", "assisted", 1},
	                                                                     {"reserve", "speed_mm_s", 0}})
	{
		require(client->command(continuous(owner, action, value)), "remote manual input and neutral release admitted");
		until(
		    [&]
		    {
			    return accepted_value(owner, action) == value;
		    });
	}
	// Walk real current descriptors for numeric buttons; every step crosses the authenticated transport.
	auto send = [&](const std::shared_ptr<SessionPort> &port, OperatingCommand input)
	{
		if (input.action == "preset" || input.action == "away")
			return port->command(input);
		std::string owner = input.system, channel = input.action;
		if (channel == "intent_integer")
		{
			owner = "preference";
			channel = "level";
		}
		if (owner.empty())
			owner = channel == "radar_enabled" ? "search" : (port == client ? "reserve" : "drive");
		for (int step = 0; step < 50; ++step)
		{
			const auto view = port->observation();
			if (!view)
				return false;
			Json section;
			for (const auto &candidate : view->gameplay.value("controllers", Json::array()))
				if (candidate.at("id") == owner)
					section = candidate;
			if (section.is_null() || !section.at("human").get<bool>() || !section.at("commands").contains(channel))
				return false;
			const auto state = section.at("commands").at(channel);
			if (input.value < state.at("minimum").get<std::int64_t>() ||
			    input.value > state.at("maximum").get<std::int64_t>())
				return false;
			const auto current = state.at("value").get<std::int64_t>();
			if (current == input.value)
				return true;
			std::optional<OperatingCommand> selected;
			for (const auto &descriptor : view->gameplay.value("controls", Json::array()))
			{
				if (descriptor.value("controller", std::string{}) != owner ||
				    descriptor.value("channel", std::string{}) != channel)
					continue;
				const auto value = descriptor.at("value").get<std::int64_t>();
				if (value == input.value || (input.value > current && value > current && value < input.value) ||
				    (input.value < current && value < current && value > input.value))
					if (!selected || std::abs(input.value - value) < std::abs(input.value - selected->value))
						selected = decode_operating_command(descriptor);
			}
			if (!selected)
				return false;
			if (!port->command(*selected))
				return false;
			const auto committed = selected->value;
			until(
			    [&]
			    {
				    const auto updated = port->observation();
				    if (!updated)
					    return false;
				    for (const auto &candidate : updated->gameplay.value("controllers", Json::array()))
					    if (candidate.at("id") == owner)
						    return candidate.at("commands").at(channel).at("value") == committed;
				    return false;
			    });
		}
		return false;
	};
	const auto controls = client->observation()->gameplay.value("controls", Json::array());
	require(std::any_of(controls.begin(), controls.end(),
	                    [](const auto &value)
	                    {
		                    return value.at("action") == "lateral_thrust" &&
		                           value.value("controller", std::string{}) == "side";
	                    }),
	        "Remote gameplay projection omitted admitted semantic controls");
	stage = "remote semantic controls";
	require(!send(client, {ship, "unprojected", 800, "storage"}) && !send(client, {ship, "preset", 99, {}}) &&
	            send(client, {ship, "preset", 1, {}}),
	        "Remote meta commands must match an exact current permitted descriptor");
	until(
	    [&]
	    {
		    return !human("storage") && remote_value("storage", "charge_limit") == 200;
	    });
	require(send(client, {ship, "intent_integer", 800, "preference"}) &&
	            !send(client, {ship, "intent_integer", 900, "preference"}),
	        "Controller preference controls must retain authored bounds");
	until(
	    [&]
	    {
		    return remote_value("storage", "charge_limit") == 800;
	    });
	// Retire the temporary high preference before direct takeover. During
	// recovery a legitimately unoccupied tick may run this same AI backfill;
	// its retained intent must agree with the human setting being recovered.
	require(send(client, {ship, "intent_integer", 400, "preference"}),
	        "Remote storage preference could not retire its temporary high intent");
	until(
	    [&]
	    {
		    return remote_value("storage", "charge_limit") == 400;
	    });
	require(send(client, {ship, "preset", 0, {}}),
	        "Remote take-control descriptor was refused for its own delegated System");
	until(
	    [&]
	    {
		    return human("storage");
	    });
	require(send(client, {ship, "preset", 0, {}}) && send(client, {ship, "away", 1, {}}),
	        "Remote direct preset and Away descriptors were refused");
	until(
	    [&]
	    {
		    return !human("reserve");
	    });
	require(!send(client, {ship, "longitudinal_thrust", 1000, "reserve"}) && !send(client, {ship, "away", 1, {}}) &&
	            send(client, {ship, "away", 0, {}}),
	        "Away must remove ordinary controls and require the current resume descriptor");
	until(
	    [&]
	    {
		    return human("reserve");
	    });
	stage = "remote paid manual controls";
	require(send(client, {ship, "assisted", 0, "reserve"}) && !send(hosts[0], {ship, "lateral_thrust", 1000, "side"}),
	        "Advanced mode admission bypassed fitted Console ownership");
	until(
	    [&]
	    {
		    return remote_value("reserve", "assisted") == 0;
	    });
	const auto manual_energy = remote_value("storage", "stored_energy");
	require(send(client, {ship, "longitudinal_thrust", -1000, "reserve"}) &&
	            send(client, {ship, "lateral_thrust", 1000, "side"}) &&
	            send(client, {ship, "charge_limit", 400, "storage"}),
	        "Remote advanced manual/power controls were refused");
	until(
	    [&]
	    {
		    return remote_value("reserve", "longitudinal_thrust") == -1000 &&
		           remote_value("reserve", "actual_longitudinal") < 0 && remote_value("side", "actual_lateral") > 0 &&
		           remote_value("storage", "charge_limit") == 400 &&
		           remote_value("storage", "stored_energy") < manual_energy;
	    });
	require(send(client, {ship, "longitudinal_thrust", 0, "reserve"}) &&
	            send(client, {ship, "lateral_thrust", 0, "side"}) && send(client, {ship, "assisted", 1, "reserve"}),
	        "Remote manual release did not admit exact neutral axes");
	until(
	    [&]
	    {
		    return remote_value("reserve", "longitudinal_thrust") == 0 && remote_value("side", "lateral_thrust") == 0 &&
		           remote_value("reserve", "assisted") == 1;
	    });
	require(send(client, {ship, "speed_mm_s", -2000, "reserve"}), "Remote signed assisted speed request was refused");
	until(
	    [&]
	    {
		    return fitted(*client->observation(), "reserve").requested == -2000 &&
		           client->observation()->requested_speed == -2000;
	    });
	require(send(hosts[0], {ship, "speed_mm_s", 2000}) && send(client, {ship, "radar_enabled", 0}),
	        "Independent fleet Console commands were refused");
	require(!send(hosts[0], {ship, "radar_enabled", 0}), "Local helm bypassed its System control boundary");
	require(!send(client, {ship, "speed_mm_s", 1000, "drive"}) && send(client, {ship, "speed_mm_s", 1000, "reserve"}),
	        "remote commands must retain exact fitted instance ownership");
	until(
	    [&]
	    {
		    return hosts[0]->observation()->requested_speed == 2000 && !client->observation()->radar_enabled &&
		           client->observation()->requested_speed == 1000 &&
		           fitted(*client->observation(), "reserve2").requested == 1000 &&
		           !client->observation()->gameplay.value("messages", Json::array()).empty();
	    });
	auto impulse_state = [&]
	{
		const auto current_view = client->observation();
		for (const auto &owner : current_view->gameplay.at("controllers"))
			if (owner.at("id") == "reserve")
				return owner.at("commands").at("impulse");
		throw std::runtime_error("Remote request section missing");
	};
	std::optional<OperatingCommand> impulse_request;
	for (const auto &descriptor : client->observation()->gameplay.value("controls", Json::array()))
		if (descriptor.value("controller", std::string{}) == "reserve" &&
		    descriptor.value("channel", std::string{}) == "impulse" && descriptor.at("value") == 1)
			impulse_request = decode_operating_command(descriptor);
	require(impulse_request && client->command(*impulse_request), "remote one-shot request admitted");
	until(
	    [&]
	    {
		    return impulse_state().at("request_id") == impulse_request->request_id &&
		           !impulse_state().at("pending").get<bool>();
	    });
	const auto consumed_impulse = impulse_state();
	require(!client->command(*impulse_request), "stale request descriptor cannot issue another impulse");
	const auto reporter_identity = client->observation()->gameplay.at("messages").front().at("id");
	const auto before = hosts[0]->ticks();
	stage = "majority checkpoint";
	hosts[2].reset();
	accelerated = true;
	const auto disconnected = std::chrono::steady_clock::now();
	until(
	    [&]
	    {
		    const bool checkpoint_ticks = hosts[0]->ticks() > before + checkpoint_frames + 4;
		    if (checkpoint_ticks)
			    advance = false;
		    return checkpoint_ticks &&
		           std::chrono::steady_clock::now() - disconnected > std::chrono::milliseconds(3500);
	    });
	advance = true;
	accelerated = false;
	hosts[2] = make(2);
	stage = "returning snapshot member";
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
	const auto checkpoint = parse_json(read_pack(portable).files().at("session.json"), "fleet checkpoint").at("state");
	const auto recovered_state = recovered.world->authoritative_world().save();
	require(checkpoint.at("schema") == 33 && recovered_state.at("schema") == 33 &&
	            checkpoint.at("scenario") == recovered_state.at("scenario") &&
	            checkpoint.at("scenario").at("runtime").at("objectives").size() == 1,
	        "Portable fleet envelope lost current-schema scenario continuation or replayed its one-shot objective");
	require(checkpoint.at("bodies").size() == recovered_state.at("bodies").size(),
	        "Portable fitted fleet recovery changed the actor roster");
	for (std::size_t i = 0; i < checkpoint.at("bodies").size(); ++i)
		require(checkpoint.at("bodies")[i].at("resources") == recovered_state.at("bodies")[i].at("resources"),
		        "Portable fleet envelope changed stored energy, power settings or solver continuation");
	require(recovered.plan.at("session") != initial.plan.at("session") &&
	            recovered.plan.at("key") != initial.plan.at("key"),
	        "Portable fleet recovery reused credentials");
	require(recovered.world->ticks() == saved_tick && !recovered.world->observation(),
	        "Portable fleet recovery changed tick or retained a live operator");
	hosts[0].reset();
	client.reset();
	{
		auto journal = open_peer_store(options.data_location + "/fleet/" +
		                               initial.plan.at("session").get<std::string>() + "/ship1.journal");
		std::optional<PeerStoreResult> stored;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!(stored = journal->poll()) && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		require(stored && stored->error.empty() &&
		            stored->restored.at("base").get<std::uint64_t>() >= checkpoint_frames &&
		            stored->restored.contains("snapshot"),
		        "Playable host did not persist its automatic checkpoint");
	}
	hosts[0] = make(0);
	client = make_network_client(config);
	advance = true;
	stage = "restored crew owner";
	until(
	    [&]
	    {
		    return hosts[0]->observation() && client->observation() && hosts[0]->ticks() > saved_tick;
	    });
	require(client->observation()->console == "sensors" && send(client, {ship, "radar_enabled", 1}),
	        "Checkpoint recovery lost approved crew ownership or sequence renewal");
	stage = "restored crew command";
	until(
	    [&]
	    {
		    return client->observation() && client->observation()->radar_enabled;
	    });
	require(impulse_state().at("request_id") == consumed_impulse.at("request_id") &&
	            impulse_state().at("consumed") == consumed_impulse.at("consumed") && !client->command(*impulse_request),
	        "checkpoint rejoin preserves consumed request identity and rejects old envelope replay");
	const auto restored_messages = client->observation()->gameplay.value("messages", Json::array());
	require(restored_messages.size() == 1 && restored_messages.front().at("id") == reporter_identity,
	        "routed physical report is not duplicated by checkpoint/rejoin");
	require_value("Durable shared second engine", fitted(*client->observation(), "reserve2").requested, 1000);
	require_value("Durable storage charge_limit", remote_value("storage", "charge_limit"), 400);
	require_value("Durable reserve minimum_speed", remote_value("reserve", "minimum_speed"), -5000);
	require_value("Durable reserve longitudinal_thrust", remote_value("reserve", "longitudinal_thrust"), 0);
	require_value("Durable side lateral_thrust", remote_value("side", "lateral_thrust"), 0);
	const auto remaining_energy = remote_value("storage", "stored_energy");
	if (remaining_energy <= 0 || remaining_energy >= manual_energy)
		throw std::runtime_error("Durable storage reserve must remain finite and depleted: before manual " +
		                         std::to_string(manual_energy) + ", observed " + std::to_string(remaining_energy));
	require_value("Durable own-ship objective count",
	              static_cast<std::int64_t>(objectives(*client->observation()).size()), 1);
	require_value("Durable other-ship objective count",
	              static_cast<std::int64_t>(objectives(*hosts[1]->observation()).size()), 0);
	std::cout << "PASS playable fleet local/remote Consoles, approval, majority continuation, durable return and "
	             "portable recovery, automatic compaction, restored crew grants, fitted power/manual controls and "
	             "scoped scenario knowledge"
	          << std::endl;
	return 0;
}
int main(int argc, char **argv)
{
	constexpr unsigned inspection_remote_bodies = 16;
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
			auto definitions =
			    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic fixture");
			large_network_knowledge(definitions);
			definitions["entities"][0]["render"]["radar_icon"] = "ship";
			definitions["entities"][1]["render"]["radar_icon"] = "asteroid";
			definitions["entities"][1]["render"]["radar_sprite"] = "test-core:assets/pixel.png";
			definitions["maps"][0]["entities"].push_back(
			    {{"id", 2}, {"definition", "test.variant"}, {"x_mm", 200000}, {"y_mm", 0}, {"heading_mdeg", 0}});
			// A real GM inspection must cross the transport envelope bound while
			// ordinary crew still receive only their in-range observation. Keep the
			// synthetic world within the separate 4 MiB inspection-state bound.
			// Leave room for each body's Controller and knowledge continuation as
			// those systems grow; the size assertion below still requires chunking.
			for (unsigned i = 10; i < 10 + inspection_remote_bodies; ++i)
				definitions["maps"][0]["entities"].push_back({{"id", i},
				                                              {"definition", "test.variant"},
				                                              {"x_mm", 1000000 + i * 5000},
				                                              {"y_mm", 1000000},
				                                              {"heading_mdeg", 0}});
			const auto pack = phoenix::test::fixture_pack(json_bytes(definitions), "test-core");
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
					    session->inspection().at("bodies").size() != 3 + inspection_remote_bodies ||
					    json_bytes(session->inspection()).size() <= 65536 ||
					    json_bytes(session->inspection()).size() > 4 * 1024 * 1024)
						throw std::runtime_error("GM approval/read-only/full-world boundary failed");
					std::cout << "PASS host-approved read-only GM inspection" << std::endl;
					return 0;
				}
			}
			else
			{
				if (!session->catalog().ordered_packs.empty() &&
				    (!session->catalog().presentation || !session->catalog().maps.empty() ||
				     !session->catalog().trees.empty() ||
				     (session->catalog().selection ? !session->catalog().entities.empty()
				                                   : session->catalog().entities.size() != 1)))
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
						if (options.link.self == "replacement" &&
						    ship_camera_state(view->gameplay) != ShipCameraState{1, 6})
							throw std::runtime_error("Replacement operator lost shared camera selection");
						if (!session->command({view->own.id, "camera_mode", 1}) ||
						    !session->command({view->own.id, "camera_direction", 6}))
							throw std::runtime_error("Remote authored camera controls refused");
						if (view->own.radar_icon != RadarIcon::ship || view->contacts.empty() ||
						    view->contacts[0].radar_icon != RadarIcon::asteroid)
							throw std::runtime_error("Remote radar lost the authored contact symbols");
						const auto &sprite = view->contacts[0].radar_sprite;
						if (sprite.empty() || !session->catalog().assets.contains(sprite) ||
						    session->catalog().entities.size() != 1 || !session->catalog().maps.empty())
							throw std::runtime_error(
							    "Remote radar lost its public PNG or exposed full contact definitions");
						if (session->command({2, "speed_mm_s", 1000}))
							throw std::runtime_error("Client admitted a command for another ship");
						std::optional<OperatingCommand> publication, message;
						for (const auto &descriptor : view->gameplay.value("controls", Json::array()))
						{
							if (descriptor.value("action", std::string{}) == "publish.network-speed")
								publication = decode_operating_command(descriptor);
							if (descriptor.value("action", std::string{}) == "message.test.network-report")
								message = decode_operating_command(descriptor);
						}
						if (!publication || !message)
							throw std::runtime_error("Remote publication/Controller message descriptor missing");
						publication->target = view->own.id;
						message->target = view->own.id;
						auto forged = *message;
						forged.action = "message.unprojected";
						auto stale = *message;
						++stale.generation;
						if (!session->command(*publication))
							throw std::runtime_error("Remote current publication descriptor refused: " +
							                         encode_operating_command(*publication).dump());
						if (!session->command(*message))
							throw std::runtime_error("Remote current message descriptor refused: " +
							                         encode_operating_command(*message).dump());
						if (session->command(forged) || session->command(stale) ||
						    session->command({view->own.id, "message.test.network-report", 0, "engine"}))
							throw std::runtime_error("Remote forged message descriptor admitted");
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
						bool report = false, publication = false;
						for (const auto &message : view->gameplay.value("messages", Json::array()))
							if (message.at("payload").at("schema") == "network-report")
							{
								const auto &fields = message.at("payload").at("fields");
								report = fields.size() == 12 && fields.at("part0") == std::string(4096, 'a') &&
								         fields.at("part11") == std::string(4096, 'l');
								if (!message.contains("sequence") || !message.at("payload").contains("observed_tick"))
									throw std::runtime_error(
									    "Remote large message lost delivery identity or provenance");
							}
						for (const auto &record : view->gameplay.value("shared", Json::array()))
							publication = publication || record.at("schema") == "network-speed";
						if (!report || !publication || ship_camera_state(view->gameplay) != ShipCameraState{1, 6} ||
						    ship_camera_state(session->public_board()) != ShipCameraState{1, 6})
							continue;
						if (view->gameplay.dump().size() <= 40000)
							throw std::runtime_error(
							    "Network knowledge fixture did not cross the old sub-envelope bound");
						std::cout
						    << "PASS remote verified content, committed claim, scoped radar, engine and steering"
						       ", shared camera, authored message/publication and chunked large knowledge projection"
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
