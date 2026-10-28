#pragma once
#include <optional>
#include <utility>
#include <vector>

// Synthetic provider over the production authenticated transport. No model API.
int inference_connection_cases(const std::string &token, const std::string &service)
{
	using namespace phoenix;
	const auto require = [](bool valid, const char *message)
	{
		if (!valid)
			throw std::runtime_error(message);
	};
	const auto room = "ai-" + sha256(Bytes(token.begin(), token.end())).substr(0, 32);
	auto provider = open_peer_transport({service, room, "bridge", true});
	InferenceConnection connection(token, service);
	std::string epoch, request;
	const auto drive = [&]
	{
		connection.poll();
		for (const auto &notice : provider->poll())
			if (notice.kind == LinkEvent::packet && notice.peer == "host")
				if (const auto packet = decode_packet(notice.data, "host", "bridge", "inference", token))
				{
					epoch = packet->body.value("epoch", epoch);
					if (packet->type == "ai-request" || packet->type == "ai-batch-part")
						request = packet->body.at("id").get<std::string>();
				}
	};
	const auto wait = [&](const auto &ready)
	{
		const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		do
		{
			drive();
			if (ready())
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		} while (std::chrono::steady_clock::now() < end);
		throw std::runtime_error("inference connection contract timed out");
	};
	const auto send = [&](const std::string &type, Json body)
	{
		body["epoch"] = epoch;
		require(provider->send("host", encode_packet({"inference", "bridge", "host", type, std::move(body)}, token)),
		        "provider has an authenticated route to the connection");
	};
	wait(
	    [&]
	    {
		    return !epoch.empty();
	    });
	const Json capabilities{{"providers", {"anyjev"}}, {"max_batch", 2}, {"decision_interval_ms", 1000}};
	send("ai-capabilities", capabilities);
	wait(
	    [&]
	    {
		    return connection.status().at("paired").get<bool>();
	    });
	auto invalid = capabilities;
	invalid["max_batch"] = 6;
	invalid["decision_interval_ms"] = 0;
	send("ai-capabilities", invalid);
	wait(
	    [&]
	    {
		    return connection.status().contains("error");
	    });
	require(connection.capacity() == 2 && connection.status().at("decision_interval_ms") == 1000,
	        "invalid provider negotiation preserves both the batch limit and decision interval");
	send("ai-capabilities", capabilities);
	wait(
	    [&]
	    {
		    return !connection.status().contains("error");
	    });
	const Json snapshot{{"provider", "anyjev"}, {"candidates", Json::array({{{"id", "go"}}})}};
	auto invalid_snapshot = snapshot;
	invalid_snapshot["candidates"] = Json::array({Json::object()});
	require(connection.request_batch(Json::array({snapshot, invalid_snapshot})), "bounded request is queued");
	wait(
	    [&]
	    {
		    return !request.empty();
	    });
	send("ai-result", {{"id", request}, {"candidates", {"go", "go"}}, {"latency_ms", 12}});
	wait(
	    [&]
	    {
		    return connection.status().contains("error");
	    });
	require(connection.busy() && connection.completed().empty() && connection.status().at("latency_ms") == 0,
	        "a rejected result cannot publish a partial batch or update latency");
	connection.reset_requests();
	request.clear();
	require(connection.request_batch(Json::array({snapshot, snapshot})), "valid batch is queued after reset");
	wait(
	    [&]
	    {
		    return !request.empty();
	    });
	send("ai-result", {{"id", request}, {"candidates", {"go", "go"}}, {"latency_ms", 12}});
	wait(
	    [&]
	    {
		    return !connection.busy();
	    });
	const auto results = connection.completed();
	require(results.size() == 2 && !results[0].at("failed").get<bool>() && !results[1].at("failed").get<bool>() &&
	            connection.completed().empty() && !connection.status().contains("error") &&
	            connection.status().at("latency_ms") == 12,
	        "a valid response recovers the connection and publishes each result exactly once");
	std::cout << "PASS atomic inference negotiation, response refusal and recovery\n";
	return 0;
}
