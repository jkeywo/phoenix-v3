#include "../support/fixtures.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <map>
import phoenix.network.host;
import phoenix.fleet_setup;
using namespace phoenix;
int main(int argc, char **argv)
{
	try
	{
		if (argc < 3)
			throw std::runtime_error("Use --prepare bundle.zip or --host bundle.zip member data-dir seconds [relay]");
		const std::string bundle_path = argv[2];
		if (std::string(argv[1]) == "--inspect")
		{
			const auto recovered = read_fleet_setup(read_file(bundle_path));
			if (argc > 3)
			{
				const auto initial = read_fleet_setup(read_file(argv[3]));
				if (recovered.plan.at("session") == initial.plan.at("session") ||
				    recovered.plan.at("key") == initial.plan.at("key") ||
				    recovered.plan.at("members") != initial.plan.at("members"))
					throw std::runtime_error(
					    "Portable recovery did not refresh its epoch/key and preserve the fixed roster");
			}
			if (!recovered.world->authoritative_world().save().at("claims").empty())
				throw std::runtime_error("Portable recovery retained a live Console claim");
			std::cout << "Verified portable fleet at tick " << recovered.world->ticks() << " with "
			          << recovered.plan.at("members").size() << " fixed members and no live claims\n";
			return 0;
		}
		if (std::string(argv[1]) == "--prepare")
		{
			auto data =
			    parse_json(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "browser fleet fixture");
			for (unsigned i = 2; i <= 3; ++i)
			{
				auto slot = data["maps"][0]["ship_slots"][0];
				slot["id"] = "slot" + std::to_string(i);
				slot["x_mm"] = i * 30000;
				data["maps"][0]["ship_slots"].push_back(slot);
			}
			auto pack = phoenix::test::fixture_pack(json_bytes(data), "test-core");
			auto source = make_host_world();
			source->add_pack(write_pack(pack));
			write_file(bundle_path, export_fleet_setup(*source, {"test.map", {{"test.slot", "test.ship"}}, "test.slot"},
			                                           "browser-fleet-smoke"));
			std::cout << "Prepared exact synthetic three-member bundle\n";
			return 0;
		}
		if (argc < 6)
			throw std::runtime_error("Missing host member, data directory or duration");
		const std::string member = argv[3];
		NetworkOptions config;
		config.link.url = "ws://127.0.0.1:9080/phoenix";
		config.link.room = "browser-fleet-smoke";
		config.link.self = "initial-" + member;
		config.link.force_relay = argc > 6 && std::string(argv[6]) == "relay";
		config.data_location = argv[4];
		config.profile = member;
		auto host = make_network_host(config);
		host->restore(read_file(bundle_path));
		if (!host->session_action("network.member/" + member))
			throw std::runtime_error("Cannot select fixed member");
		host->resume_local();
		const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(std::stoi(argv[5]));
		auto tick_at = std::chrono::steady_clock::now(), report_at = tick_at;
		while (std::chrono::steady_clock::now() < end)
		{
			host->poll();
			const auto now = std::chrono::steady_clock::now();
			if (now >= tick_at)
			{
				host->tick();
				tick_at = now + std::chrono::milliseconds(50);
			}
			// Explicitly synthetic smoke host, never a production approval policy.
			for (const auto &[id, label] : host->lobby_controls())
				if (id.starts_with("network.approve/") || id.starts_with("network.gm/"))
					host->session_action(id);
			if (now >= report_at)
			{
				std::cout << member << " tick " << host->ticks() << " / " << host->connection_status() << std::endl;
				if (host->running())
					write_file(config.data_location + ".world.json", json_bytes(host->inspection()));
				report_at = now + std::chrono::seconds(1);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return host->ticks() > 10 ? 0 : 1;
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
