#include <chrono>
#include <iostream>
#include <thread>
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

using namespace phoenix;
int main(int argc, char **argv)
{
	NetworkOptions options;
	options.link.url = "ws://127.0.0.1:9080/phoenix";
	options.link.room = "synthetic-session";
	options.server = "host";
	options.data_location = "build/session-probe";
	bool host = false, confirm = false, blocked = false, approve = false;
	unsigned seconds = 20;
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "--host")
			host = true;
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
