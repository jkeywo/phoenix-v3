// Explicit real-transport measurement, not a CI performance threshold.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import phoenix.data;
import phoenix.content.catalog;
import phoenix.content.packs;
import phoenix.network.client;
import phoenix.network.host;
import phoenix.session.api;

using namespace phoenix;

int main(int argc, char **argv)
try
{
	if (argc < 9)
		throw std::runtime_error(
		    "Usage: phoenix_network_profile pack scenario slot ship clients seconds output url [gm] [relay]");
	const std::string pack_path = argv[1], scenario = argv[2], slot = argv[3], ship = argv[4], output = argv[7];
	const unsigned count = std::stoul(argv[5]), seconds = std::stoul(argv[6]);
	if (!count || count > 5 || seconds < 10 || seconds > 600)
		throw std::runtime_error("Network profile requires 1-5 clients and 10-600 measured seconds");
	const bool gm = argc > 9 && std::string(argv[9]) == "gm";
	const bool relay = argc > 10 && std::string(argv[10]) == "relay";
	std::filesystem::create_directories(output);
	using Clock = std::chrono::steady_clock;
	const auto preparation_started = Clock::now();
	std::cout << "PHOENIX_NETWORK_LOADING" << std::endl;
	const auto archive = read_file(pack_path);
	const auto pack = read_pack(archive);
	NetworkOptions options;
	options.expected_base = base_configuration(pack);
	options.link.url = argv[8];
	options.link.room = "profile-" + random_token();
	options.link.self = options.server = "profile-host";
	options.link.force_relay = relay;
	options.data_location = output;
	options.profile = "host";
	auto host = make_network_host(options);
	host->add_pack(archive);
	host->start({scenario, {{slot, ship}}, slot, {}});
	std::vector<std::string> consoles;
	for (const auto &seat : host->console_seats())
		if (!seat.spec.human_seeking)
			consoles.push_back(seat.address.console);
	if (consoles.size() < count)
		throw std::runtime_error("Selected ship has fewer ordinary Consoles than requested clients");
	std::vector<std::shared_ptr<SessionPort>> clients;
	for (unsigned i = 0; i < count + unsigned(gm); ++i)
	{
		auto client = options;
		client.inspector = i == count;
		client.link.self = client.profile = "profile-client-" + std::to_string(i);
		clients.push_back(make_network_client(std::move(client)));
	}
	const auto started = Clock::now();
	const auto preparation_seconds = std::chrono::duration<double>(started - preparation_started).count();
	std::cout << "PHOENIX_NETWORK_JOINING " << Json{{"preparation_seconds", preparation_seconds}}.dump() << std::endl;
	auto next_tick = started, next_claim = started, warmup_started = Clock::time_point{},
	     measured = Clock::time_point{}, next_status = started + std::chrono::seconds(10);
	std::vector<std::shared_ptr<const Observation>> previous(count);
	std::vector<std::uint64_t> publications(count), missing(count), maximum_lag(count);
	std::uint64_t initial_tick = 0;
	bool gm_ready = !gm;
	for (;;)
	{
		host->poll();
		for (const auto &[id, label] : host->lobby_controls())
			if (id.starts_with("network.approve/") || id.starts_with("network.gm/"))
				host->session_action(id);
		const auto now = Clock::now();
		if (now >= next_tick)
		{
			host->tick();
			next_tick = now + std::chrono::milliseconds(50);
		}
		bool ready = true;
		unsigned ready_consoles = 0;
		for (unsigned i = 0; i < clients.size(); ++i)
		{
			auto &client = *clients[i];
			client.poll();
			if (i == count)
			{
				if (!gm_ready && client.running() && client.ticks())
					gm_ready = !client.inspection().is_null();
				ready &= gm_ready;
				continue;
			}
			auto view = client.console_snapshot(1);
			if (!view)
			{
				ready = false;
				if (measured != Clock::time_point{})
					++missing[i];
				if (now >= next_claim)
					for (const auto &[id, label] : client.lobby_controls())
						if (id == "network.claim/" + consoles[i] || (count == 1 && id == "network.claim"))
							client.session_action(id);
				continue;
			}
			if (measured != Clock::time_point{} && view != previous[i])
			{
				++publications[i];
				if (host->ticks() >= view->observed_tick)
					maximum_lag[i] = std::max(maximum_lag[i], host->ticks() - view->observed_tick);
			}
			++ready_consoles;
			previous[i] = std::move(view);
		}
		if (now >= next_status)
		{
			std::cout
			    << "PHOENIX_NETWORK_STATUS "
			    << Json{{"tick", host->ticks()}, {"ready_consoles", ready_consoles}, {"gm_ready", gm_ready}}.dump()
			    << std::endl;
			next_status = now + std::chrono::seconds(10);
		}
		if (now >= next_claim)
			next_claim = now + std::chrono::seconds(1);
		if (measured == Clock::time_point{})
		{
			if (!ready)
				warmup_started = {};
			else if (warmup_started == Clock::time_point{})
				warmup_started = now;
			else if (now - warmup_started >= std::chrono::seconds(5))
			{
				measured = now;
				initial_tick = host->ticks();
				std::cout << "PHOENIX_NETWORK_READY "
				          << Json{{"clients", count}, {"gm", gm}, {"initial_tick", initial_tick}}.dump() << std::endl;
			}
			if (now - started > std::chrono::minutes(10))
				throw std::runtime_error("Network profile did not admit every requested Console/GM before deadline");
		}
		else if (now - measured >= std::chrono::seconds(seconds))
		{
			Json peers = Json::array();
			for (unsigned i = 0; i < count; ++i)
				peers.push_back({{"console", consoles[i]},
				                 {"publications", publications[i]},
				                 {"missing_observation_polls", missing[i]},
				                 {"maximum_lag_ticks", maximum_lag[i]}});
			const Json report{{"schema", 1},
			                  {"scenario", scenario},
			                  {"ship", ship},
			                  {"pack_hash", pack.hash()},
			                  {"clients", count},
			                  {"gm", gm},
			                  {"force_relay", relay},
			                  {"capture_seconds", std::chrono::duration<double>(now - measured).count()},
			                  {"startup_seconds", std::chrono::duration<double>(measured - started).count()},
			                  {"preparation_seconds", preparation_seconds},
			                  {"captured_ticks", host->ticks() - initial_tick},
			                  {"peers", peers}};
			std::ofstream file(output + "/session.json");
			file.exceptions(std::ios::badbit | std::ios::failbit);
			file << report.dump(2) << '\n';
			std::cout << "PHOENIX_NETWORK_PROFILE " << report.dump() << std::endl;
			return std::any_of(missing.begin(), missing.end(),
			                   [](auto value)
			                   {
				                   return value != 0;
			                   })
			           ? 1
			           : 0;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
catch (const std::exception &error)
{
	std::cerr << error.what() << '\n';
	return 1;
}
