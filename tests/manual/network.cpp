#include <phoenix/transport.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
using namespace phoenix;
using Json = nlohmann::json;
std::unique_ptr<PeerTransport> link;
LinkOptions options;
std::string remote = "native";
bool echo_mode = false, done = false, success = false;
unsigned sequence = 0;
std::chrono::steady_clock::time_point deadline;
void report(const std::string &text)
{
	std::cout << text << std::endl;
#ifdef __EMSCRIPTEN__
	EM_ASM({ document.getElementById('result').textContent = UTF8ToString($0); },text.c_str());
#endif
}
void send_probe()
{
	link->send(remote, Json{{"kind", "transport-probe"}, {"nonce", "synthetic-v3"}, {"sequence", ++sequence}}.dump());
}
void frame()
{
	try
	{
		for (const auto &event : link->poll())
		{
			if (event.kind == LinkEvent::error)
				report("Transport: " + event.data);
			if (!echo_mode && event.peer == remote &&
			    (event.kind == LinkEvent::discovered || event.kind == LinkEvent::direct_ready))
				send_probe();
			if (event.kind != LinkEvent::packet)
				continue;
			auto packet = Json::parse(event.data);
			if (packet.value("nonce", std::string{}) != "synthetic-v3")
				continue;
			if (echo_mode && packet.at("kind") == "transport-probe")
			{
				packet["kind"] = "transport-reply";
				packet["received_direct"] = event.direct;
				link->send(event.peer, packet.dump());
				report(std::string("Echoed ") + (event.direct ? "direct" : "relay") + " packet from " + event.peer);
			}
			else if (!echo_mode && event.peer == remote && packet.at("kind") == "transport-reply")
			{
				bool direct = event.direct && packet.at("received_direct").get<bool>();
				if ((options.force_relay && !event.direct && !packet.at("received_direct").get<bool>()) ||
				    (!options.force_relay && direct))
				{
					success = true;
					done = true;
					report(options.force_relay ? "PASS forced application relay round trip"
					                           : "PASS direct WebRTC round trip");
					break;
				}
			}
		}
		if (std::chrono::steady_clock::now() >= deadline)
		{
			done = true;
			success = echo_mode;
			if (!echo_mode)
				report("FAIL expected transport round trip timed out");
		}
	}
	catch (const std::exception &error)
	{
		done = true;
		report(std::string("FAIL ") + error.what());
	}
#ifdef __EMSCRIPTEN__
	if (done)
		emscripten_cancel_main_loop();
#endif
}
int main(int argc, char **argv)
{
	options.url = "ws://127.0.0.1:9080/phoenix";
	options.room = "synthetic-network";
	options.self = "browser";
	unsigned seconds = 30;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--echo")
		{
			echo_mode = true;
			options.self = "native";
		}
		else if (argument == "--relay")
			options.force_relay = true;
		else if (i + 1 < argc)
		{
			const std::string value = argv[++i];
			if (argument == "--url")
				options.url = value;
			if (argument == "--room")
				options.room = value;
			if (argument == "--self")
				options.self = value;
			if (argument == "--remote")
				remote = value;
			if (argument == "--ca")
				options.ca_certificate_file = value;
			if (argument == "--seconds")
				seconds = static_cast<unsigned>(std::stoul(value));
		}
	}
	try
	{
		deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
		link = open_peer_transport(options);
		report("Connecting C++ transport probe...");
#ifdef __EMSCRIPTEN__
		emscripten_set_main_loop(frame, 0, true);
#else
		while (!done)
		{
			frame();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
#endif
	}
	catch (const std::exception &error)
	{
		report(std::string("FAIL ") + error.what());
		return 1;
	}
	return success ? 0 : 1;
}
