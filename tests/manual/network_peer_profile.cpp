// Manually invoked separate-process host/client traffic measurement.
#include <algorithm>
#include <chrono>
#include <cstdlib>
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
using ProfileClock = std::chrono::steady_clock;

void save_report(const std::filesystem::path &path, const Json &value)
{
	std::ofstream file(path);
	file.exceptions(std::ios::badbit | std::ios::failbit);
	file << value.dump(2) << '\n';
}

int main(int argc, char **argv)
try
{
	const bool host = argc > 1 && std::string(argv[1]) == "host";
	if ((host && argc != 11) || (!host && (argc != 10 || std::string(argv[1]) != "client")))
		throw std::runtime_error(
		    "Usage: network_peer_profile host pack scenario slot ship clients seconds output url room; "
		    "or client pack console index seconds output url room crew|gm");
	const auto preparing = ProfileClock::now();
	const auto milestone = [&](const char *stage)
	{
		std::cout << "PHOENIX_PEER_MILESTONE "
		          << Json{{"stage", stage},
		                  {"seconds", std::chrono::duration<double>(ProfileClock::now() - preparing).count()}}
		                 .dump()
		          << std::endl;
	};
	const unsigned seconds = std::stoul(argv[host ? 7 : 5]);
	const unsigned index = host ? 0 : std::stoul(argv[4]);
	const unsigned count = host ? std::stoul(argv[6]) : 0;
	if (seconds < 10 || seconds > 600 || (host && (!count || count > 5)))
		throw std::runtime_error("Requires 10-600 measured seconds and 1-5 crew clients");
	const bool gm = !host && std::string(argv[9]) == "gm";
	const std::filesystem::path output = argv[host ? 8 : 6];
	std::filesystem::create_directories(output);
	std::cout << "PHOENIX_PEER_LOADING " << (host ? "host" : "client") << std::endl;
	Bytes archive;
	std::string pack_hash;
	NetworkOptions options;
	{
		archive = read_file(argv[2]);
		const auto pack = read_pack(archive);
		options.expected_base = base_configuration(pack);
		pack_hash = pack.hash();
	}
	milestone("archive_loaded");
	if (gm && std::getenv("PHOENIX_PROFILE_WARM_GM"))
		options.prepared_clients[pack_hash] = archive;
	if (!host)
	{
		archive.clear();
		archive.shrink_to_fit();
	}
	options.link.url = argv[host ? 9 : 7];
	options.pack_locations[pack_hash] = std::filesystem::absolute(argv[2]).string();
	options.link.room = argv[host ? 10 : 8];
	options.server = "profile-host";
	options.link.self = host ? options.server : "profile-client-" + std::to_string(index);
	options.link.force_relay = std::getenv("PHOENIX_PROFILE_DIRECT") == nullptr;
	options.profile = options.link.self;
	options.data_location = output.string();
	options.inspector = gm;
	std::shared_ptr<SessionPort> session = host ? make_network_host(options) : make_network_client(options);
	if (host)
	{
		session->add_pack(archive);
		session->start({argv[3], {{argv[4], argv[5]}}, argv[4], {}});
		milestone("world_prepared");
		while (!session->preparation_phase().empty())
		{
			session->poll();
			if (ProfileClock::now() - preparing > std::chrono::minutes(10))
				throw std::runtime_error("Host content preparation exceeded diagnostic deadline");
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		milestone("presentation_prepared");
		std::vector<std::string> consoles;
		for (const auto &seat : session->console_seats())
			if (!seat.spec.human_seeking)
				consoles.push_back(seat.address.console);
		if (consoles.size() < count)
			throw std::runtime_error("Ship has fewer ordinary Consoles than requested");
		consoles.resize(count);
		archive.clear();
		archive.shrink_to_fit();
		std::cout << "PHOENIX_PEER_HOST_READY "
		          << Json{{"consoles", consoles},
		                  {"pack_hash", pack_hash},
		                  {"preparation_seconds",
		                   std::chrono::duration<double>(ProfileClock::now() - preparing).count()}}
		                 .dump()
		          << std::endl;
	}
	const auto started = ProfileClock::now();
	auto next_tick = started, next_claim = started, next_status = started + std::chrono::seconds(10),
	     stable_since = ProfileClock::time_point{}, measured = ProfileClock::time_point{}, last_publication = started,
	     previous_sample = started;
	bool announced = false, reported = false, inspected = false, was_missing = false, content_seen = false,
	     claim_seen = false, observation_seen = false;
	std::shared_ptr<const Observation> previous;
	std::uint64_t initial_tick = 0, publications = 0, missing = 0, last_observed = 0;
	double maximum_gap = 0, missing_seconds = 0;
	for (;;)
	{
		if (std::filesystem::exists(output / "measure.abort"))
			throw std::runtime_error("Traffic diagnostic aborted by its coordinator");
		session->poll();
		auto now = ProfileClock::now();
		if (host)
		{
			for (const auto &[id, label] : session->lobby_controls())
				if (id.starts_with("network.approve/") || id.starts_with("network.gm/"))
					session->session_action(id);
			if (now >= next_tick)
			{
				session->tick();
				next_tick = ProfileClock::now() + std::chrono::milliseconds(50);
			}
			now = ProfileClock::now();
		}
		if (measured != ProfileClock::time_point{} && was_missing)
			missing_seconds += std::chrono::duration<double>(now - previous_sample).count();
		previous_sample = now;
		bool ready = host;
		if (!host)
		{
			if (!content_seen && session->catalog().presentation)
			{
				content_seen = true;
				milestone("content_verified");
			}
			auto view = gm ? std::shared_ptr<const Observation>{} : session->console_snapshot(1);
			if (view && !observation_seen)
			{
				observation_seen = true;
				milestone("first_projection");
			}
			if (gm && !inspected && session->running() && session->ticks())
				inspected = !session->inspection().is_null();
			ready = gm ? inspected && session->running() : bool(view);
			was_missing = !ready;
			if (!ready && measured != ProfileClock::time_point{})
				++missing;
			if (!gm && !view && now >= next_claim)
				for (const auto &[id, label] : session->lobby_controls())
					if (id == "network.claim/" + std::string(argv[3]))
					{
						if (!claim_seen)
						{
							claim_seen = true;
							milestone("claim_requested");
						}
						session->session_action(id);
					}
			const auto observed = gm ? session->ticks() : view ? view->observed_tick : 0;
			const bool changed = gm ? observed != last_observed : view && view != previous;
			if (ready && measured != ProfileClock::time_point{})
			{
				maximum_gap = std::max(maximum_gap, std::chrono::duration<double>(now - last_publication).count());
				if (changed)
				{
					++publications;
					last_publication = now;
				}
			}
			last_observed = observed;
			previous = std::move(view);
			if (!ready)
			{
				stable_since = {};
				if (announced)
				{
					announced = false;
					std::cout << "PHOENIX_PEER_CLIENT_WAIT " << Json{{"index", index}}.dump() << std::endl;
				}
			}
			else if (stable_since == ProfileClock::time_point{})
				stable_since = now;
			else if (!announced && now - stable_since >= std::chrono::seconds(5))
			{
				announced = true;
				std::cout << "PHOENIX_PEER_CLIENT_READY "
				          << Json{{"index", index}, {"gm", gm}, {"observed_tick", observed}}.dump() << std::endl;
			}
		}
		if (now >= next_claim)
			next_claim = now + std::chrono::seconds(1);
		if (now >= next_status)
		{
			std::cout << "PHOENIX_PEER_STATUS "
			          << Json{{"peer", options.link.self},
			                  {"ready", ready},
			                  {"tick", session->ticks()},
			                  {"status", session->connection_status()}}
			                 .dump()
			          << std::endl;
			next_status = now + std::chrono::seconds(10);
		}
		if (measured == ProfileClock::time_point{})
		{
			if (std::filesystem::exists(output / "measure.start"))
			{
				measured = last_publication = previous_sample = now;
				initial_tick = session->ticks();
			}
			else if (now - started > std::chrono::minutes(10))
				throw std::runtime_error("Separate-process admission did not become ready before deadline");
		}
		else if (!reported && now - measured >= std::chrono::seconds(seconds))
		{
			const Json report{
			    {"role", host ? "host"
			             : gm ? "gm"
			                  : "crew"},
			    {"peer", options.link.self},
			    {"console", host || gm ? "" : argv[3]},
			    {"pack_hash", pack_hash},
			    {"capture_seconds", std::chrono::duration<double>(now - measured).count()},
			    {"captured_ticks", session->ticks() >= initial_tick ? session->ticks() - initial_tick : 0},
			    {"final_tick", session->ticks()},
			    {"final_observed_tick", last_observed},
			    {"publications", publications},
			    {"missing_observation_polls", missing},
			    {"missing_observation_seconds", missing_seconds},
			    {"maximum_snapshot_gap_seconds", maximum_gap},
			    {"preparation_seconds", std::chrono::duration<double>(started - preparing).count()},
			    {"startup_seconds", std::chrono::duration<double>(measured - started).count()}};
			save_report(output / (options.link.self + ".json"), report);
			std::cout << "PHOENIX_PEER_REPORT " << report.dump() << std::endl;
			reported = true;
			if (!host)
				return missing ? 1 : 0;
		}
		if (host && reported &&
		    (std::filesystem::exists(output / "measure.stop") || now - measured > std::chrono::seconds(seconds + 30)))
			return 0;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
catch (const std::exception &error)
{
	std::cerr << error.what() << std::endl;
	return 1;
}
