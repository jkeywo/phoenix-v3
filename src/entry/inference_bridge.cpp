#include <chrono>
#include <condition_variable>
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
		struct Output
		{
			std::mutex mutex;
			std::condition_variable ready;
			std::string pending;
		};
		auto output = std::make_shared<Output>();
		std::thread(
		    [output]
		    {
			    for (;;)
			    {
				    std::string line;
				    {
					    std::unique_lock lock(output->mutex);
					    output->ready.wait(lock,
					                       [&]
					                       {
						                       return !output->pending.empty();
					                       });
					    line.swap(output->pending);
				    }
				    std::cout << line << std::endl;
			    }
		    })
		    .detach();
		auto emit = [output](const Json &request)
		{
			std::lock_guard lock(output->mutex);
			output->pending = request.dump(); // Keep only the newest waiting job if the worker is busy.
			output->ready.notify_one();
		};
		std::string epoch, last_id;
		Json parts = Json::array();
		bool submitted = false;
		Json providers = Json::array();
		std::uint64_t max_batch = 1, decision_interval_ms = 2000;
		auto last_capabilities = std::chrono::steady_clock::now();
		for (;;)
		{
			{
				std::lock_guard lock(input->mutex);
				if (input->closed)
					break;
				for (const auto &value : input->queue)
					if (value.value("type", std::string{}) == "capabilities")
					{
						providers = value.at("providers");
						max_batch = checked_unsigned(value.value("max_batch", Json(1)), 1, 6, "bridge batch");
						decision_interval_ms = checked_unsigned(value.value("decision_interval_ms", Json(2000)), 500,
						                                        2000, "decision interval");
					}
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
							{
								last_id.clear();
								parts = Json::array();
								submitted = false;
							}
							epoch = next;
						}
						else if (packet->type == "ai-batch-part" && packet->body.at("epoch") == epoch && !epoch.empty())
						{
							const auto &body = packet->body;
							const auto count = checked_unsigned(body.at("count"), 2, 6, "batch parts");
							const auto part = checked_unsigned(body.at("part"), 0, count - 1, "batch part");
							const auto id = body.at("id").get<std::string>();
							if (body.at("snapshot").dump().size() > 48000)
								continue;
							if (id != last_id)
							{
								last_id = id;
								parts = Json::array();
								for (unsigned i = 0; i < count; ++i)
									parts.push_back(nullptr);
								submitted = false;
							}
							if (submitted || parts.size() != count)
								continue;
							if (parts[part].is_null())
								parts[part] = body.at("snapshot");
							bool complete = true;
							for (const auto &p : parts)
								complete = complete && !p.is_null();
							if (complete)
							{
								submitted = true;
								emit(Json{{"id", id}, {"epoch", epoch}, {"snapshots", parts}});
							}
						}
						else if (packet->type == "ai-request" && packet->body.at("epoch") == epoch && !epoch.empty())
						{
							const auto id = packet->body.at("id").get<std::string>();
							if (id != last_id)
							{
								last_id = id;
								emit(packet->body);
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
				                                  {{"epoch", epoch},
				                                   {"providers", providers},
				                                   {"max_batch", max_batch},
				                                   {"decision_interval_ms", decision_interval_ms}}},
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
