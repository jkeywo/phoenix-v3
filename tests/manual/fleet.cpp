#include "../support/fixtures.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

import phoenix.fleet;
import phoenix.simulation;
import phoenix.wire;

using namespace phoenix;
namespace
{
class Partition final : public PeerTransport
{
	std::unique_ptr<PeerTransport> route_;
	std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
	unsigned drop_, heal_;
	bool blocked() const
	{
		const auto time =
		    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
		return drop_ && time >= drop_ && (!heal_ || time < heal_);
	}

public:
	std::uint64_t direct_packets = 0, relay_packets = 0;
	Partition(LinkOptions options, unsigned drop, unsigned heal)
	    : route_(open_peer_transport(options)), drop_(drop), heal_(heal)
	{
	}
	std::vector<LinkNotice> poll() override
	{
		auto notices = route_->poll();
		for (const auto &notice : notices)
			if (notice.kind == LinkEvent::packet)
			{
				if (notice.direct)
					++direct_packets;
				else
					++relay_packets;
			}
		if (blocked())
			notices.erase(std::remove_if(notices.begin(), notices.end(),
			                             [](const auto &notice)
			                             {
				                             return notice.kind == LinkEvent::packet;
			                             }),
			              notices.end());
		return notices;
	}
	bool send(const std::string &peer, const std::string &bytes) override
	{
		return blocked() ? true : route_->send(peer, bytes);
	}
};
} // namespace
int main(int argc, char **argv)
{
	try
	{
		FleetOptions options;
		options.link.url = "ws://127.0.0.1:9080/phoenix";
		options.link.room = "fleet-probe";
		options.roster.session = "fleet-probe";
		options.roster.members = {"a", "b", "c"};
		std::string output, fixture, data = "build/fleet-probe";
		unsigned seconds = 16, drop = 0, heal = 0;
		bool mismatch = false, prepare = false;
		for (int i = 1; i < argc; ++i)
		{
			const std::string argument = argv[i];
			if (argument == "--mismatch")
				mismatch = true;
			else if (argument == "--prepare")
				prepare = true;
			else if (argument == "--relay")
				options.link.force_relay = true;
			else if (i + 1 < argc)
			{
				const std::string value = argv[++i];
				if (argument == "--url")
					options.link.url = value;
				if (argument == "--room")
					options.link.room = value;
				if (argument == "--session")
					options.roster.session = value;
				if (argument == "--self")
					options.link.self = value;
				if (argument == "--key")
					options.key = value;
				if (argument == "--data")
					data = value;
				if (argument == "--fixture")
					fixture = value;
				if (argument == "--result")
					output = value;
				if (argument == "--seconds")
					seconds = std::stoul(value);
				if (argument == "--drop")
					drop = std::stoul(value);
				if (argument == "--heal")
					heal = std::stoul(value);
				if (argument == "--members")
				{
					options.roster.members.clear();
					std::stringstream values(value);
					std::string member;
					while (std::getline(values, member, ','))
						options.roster.members.push_back(member);
				}
			}
		}
		options.roster.self = options.link.self;
		options.journal = data + "/" + options.roster.self + ".journal";
		auto definitions =
		    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "synthetic fleet fixture");
		phoenix::test::navigation_policy(definitions["entities"][0], "test.patrol");
		definitions["trees"] = Json::parse(R"json([
  {
    "id": "test.patrol",
    "context": "controller",
    "initial": "run",
    "command_bindings": [
      {
        "channel": "heading_mdeg",
        "type": "integer",
        "semantics": "setting"
      },
      {
        "channel": "speed_mm_s",
        "type": "integer",
        "semantics": "setting"
      }
    ],
    "states": [
      {
        "id": "run",
        "running": [
          {
            "action": "write.heading_mdeg",
            "arguments": [
              45000
            ],
            "once": true
          },
          {
            "action": "write.speed_mm_s",
            "arguments": [
              2000
            ],
            "once": true
          }
        ],
        "transitions": [
          {
            "target": "run",
            "conditions": [
              {
                "left": {
                  "elapsed": true
                },
                "op": "ge",
                "right": 9
              }
            ]
          }
        ]
      }
    ]
  }
])json");
		definitions["maps"][0]["entities"] = Json::array();
		definitions["maps"][0]["ship_slots"] = Json::array();
		std::map<std::string, std::string> ships;
		std::map<std::string, std::uint64_t> consoles;
		int index = 0;
		for (const auto &peer : options.roster.members)
		{
			ships[peer] = "test.ship";
			consoles[peer] = ++index;
			definitions["maps"][0]["ship_slots"].push_back({{"id", peer},
			                                                {"x_mm", index * 50000},
			                                                {"y_mm", 0},
			                                                {"heading_mdeg", 0},
			                                                {"allowed_ships", Json::array({"test.ship"})}});
		}
		if (mismatch)
			definitions["entities"][0]["systems"]["engine"]["max_speed_mm_s"] = 18000;
		auto pack = phoenix::test::fixture_pack(json_bytes(definitions), "test-core");
		if (prepare)
		{
			if (fixture.empty())
				throw std::runtime_error("Fixture output path is required");
			const auto bytes = write_pack(pack);
			resolve_packs({read_pack(bytes)});
			write_file(fixture, bytes);
			return 0;
		}
		const auto catalog = resolve_packs({read_pack(fixture.empty() ? write_pack(pack) : read_file(fixture))});
		World world;
		world.load(catalog, "test.map", ships);
		options.fingerprint = sha256(json_bytes(world.save()));
		auto route = std::make_unique<Partition>(options.link, drop, heal);
		const auto *traffic = route.get();
		FleetCommit fleet(options, std::move(route));
		const auto start = std::chrono::steady_clock::now(), deadline = start + std::chrono::seconds(seconds);
		auto next = start;
		bool requested = false, finished = false;
		unsigned absent = 0, rejoined = 0;
		std::string status;
		std::uint64_t paused_tick = 0;
		bool checked_pause = false;
		while (std::chrono::steady_clock::now() < deadline)
		{
			fleet.poll();
			if (fleet.failed())
				throw std::runtime_error(fleet.status());
			for (const auto &entry : fleet.take_committed())
			{
				const auto &value = entry.value;
				const std::string kind = value.at("kind");
				if (kind == "fleet-start")
					for (const auto &[peer, console] : consoles)
					{
						if (!world.claim(console, *world.ship_for_slot(peer)))
							throw std::runtime_error("Initial fleet station claim failed");
					}
				else if (kind == "fleet-presence")
				{
					const std::string peer = value.at("peer");
					world.release(consoles.at(peer));
					if (value.at("boot") == "")
						++absent;
					else
					{
						++rejoined;
						if (!world.claim(consoles.at(peer), *world.ship_for_slot(peer)))
							throw std::runtime_error("Returning fleet station claim failed");
						if (peer == options.roster.self)
							requested = false;
					}
				}
				else if (kind == "fleet-frame")
				{
					const auto &frame = value.at("value");
					const std::string action = frame.at("kind");
					if (action == "request")
					{
						const std::string peer = value.at("origin");
						const auto target = *world.ship_for_slot(peer);
						if (!world.submit(consoles.at(peer), {target, "speed_mm_s", 1000}) ||
						    !world.submit(consoles.at(peer), {target, "heading_mdeg", 90000}))
							throw std::runtime_error("Committed fleet input failed production admission");
					}
					else if (action == "tick")
					{
						if (frame.at("tick") != world.ticks() + 1)
							throw std::runtime_error("Non-contiguous committed fleet simulation tick");
						world.tick();
					}
					else if (action == "finish")
						finished = true;
				}
			}
			const auto now = std::chrono::steady_clock::now();
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
			if (!finished && fleet.available() && !requested)
				requested = fleet.propose({{"kind", "request"}});
			if (!finished && now >= next)
			{
				if (ms >= static_cast<long long>(seconds - 3) * 1000)
					fleet.propose_tick({{"kind", "finish"}});
				else
					fleet.propose_tick({{"kind", "tick"}, {"tick", world.ticks() + 1}});
				next = now + std::chrono::milliseconds(50);
			}
			if (drop && ms > drop + 2000 && (!heal || ms < heal - 500))
			{
				if (!checked_pause)
				{
					paused_tick = world.ticks();
					checked_pause = true;
				}
				else if (world.ticks() != paused_tick)
					throw std::runtime_error("Isolated minority continued simulation without a majority");
			}
			const auto current = fleet.status();
			if (current != status)
			{
				std::cout << options.roster.self << ": " << current << std::endl;
				status = current;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Json motion = Json::array();
		for (const auto &body : world.inspect())
			motion.push_back({{"id", body.id},
			                  {"x", body.x},
			                  {"y", body.y},
			                  {"vx", body.vx},
			                  {"vy", body.vy},
			                  {"heading", body.heading}});
		Json result{{"ticks", world.ticks()},
		            {"motion", motion},
		            {"hash", sha256(json_bytes(world.save()))},
		            {"finished", finished},
		            {"absences", absent},
		            {"rejoins", rejoined},
		            {"pause_checked", checked_pause},
		            {"commit", fleet.commit_index()},
		            {"direct_packets", traffic->direct_packets},
		            {"relay_packets", traffic->relay_packets}};
		if (!output.empty())
			write_file(output, json_bytes(result));
		std::cout << result.dump() << std::endl;
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << std::endl;
		return 1;
	}
}
