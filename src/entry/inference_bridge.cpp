#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
import phoenix.inference;
import phoenix.wire;
using namespace phoenix;

int main(int argc, char **argv)
{
	try
	{
		std::string token, service = default_network_service;
		for (int i = 1; i + 1 < argc; i += 2)
			if (std::string(argv[i]) == "--pair")
				token = argv[i + 1];
			else if (std::string(argv[i]) == "--service")
				service = argv[i + 1];
			else
				throw std::runtime_error("Unknown bridge option");
		if (!valid_hex_token(token))
			throw std::runtime_error("Supply a valid pairing token");
		auto link =
		    open_peer_transport({service, "ai-" + sha256(Bytes(token.begin(), token.end())).substr(0, 32), "bridge"});
		struct Input
		{
			std::mutex mutex;
			std::deque<Json> queue;
			bool closed = false;
		};
		auto input = std::make_shared<Input>();
		std::thread(
		    [input]
		    {
			    std::string line;
			    while (std::getline(std::cin, line))
			    {
				    if (line.size() > 65536)
					    continue;
				    auto value = Json::parse(line, nullptr, false);
				    if (value.is_discarded())
					    continue;
				    std::lock_guard lock(input->mutex);
				    if (input->queue.size() < 32)
					    input->queue.push_back(std::move(value));
			    }
			    std::lock_guard lock(input->mutex);
			    input->closed = true;
		    })
		    .detach();
		std::string epoch, last_id;
		Json providers = Json::array();
		auto last_capabilities = std::chrono::steady_clock::now();
		for (;;)
		{
			{
				std::lock_guard lock(input->mutex);
				if (input->closed)
					break;
				for (const auto &value : input->queue)
					if (value.value("type", std::string{}) == "capabilities")
						providers = value.at("providers");
					else if (!epoch.empty() && value.value("id", std::string{}) == last_id)
					{
						auto result = value;
						result["epoch"] = epoch;
						link->send("host", encode_packet({"inference", "bridge", "host", "ai-result", result}, token));
					}
				input->queue.clear();
			}
			for (const auto &notice : link->poll())
				if (notice.kind == LinkEvent::packet && notice.peer == "host")
					if (const auto packet = decode_packet(notice.data, "host", "bridge", "inference", token))
					{
						if (packet->type == "ai-hello")
						{
							const auto next = packet->body.at("epoch").get<std::string>();
							if (next != epoch)
								last_id.clear();
							epoch = next;
						}
						else if (packet->type == "ai-request" && packet->body.at("epoch") == epoch && !epoch.empty())
						{
							const auto id = packet->body.at("id").get<std::string>();
							if (id != last_id)
							{
								last_id = id;
								std::cout << packet->body.dump() << std::endl;
							}
						}
					}
			const auto now = std::chrono::steady_clock::now();
			if (!epoch.empty() && now - last_capabilities > std::chrono::milliseconds(500))
			{
				link->send("host", encode_packet({"inference",
				                                  "bridge",
				                                  "host",
				                                  "ai-capabilities",
				                                  {{"epoch", epoch}, {"providers", providers}}},
				                                 token));
				last_capabilities = now;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
