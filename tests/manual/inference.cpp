#include "../support/controller_fixture.hpp"
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <thread>
import phoenix.simulation;
import phoenix.inference;
import phoenix.wire;
import phoenix.control.inference_host;
using namespace phoenix;
#include "officer_inference.hpp"
#include "../support/inference_connection_cases.hpp"
int main(int argc, char **argv)
{
	try
	{
		if (argc == 4 && std::string(argv[1]) == "--connection-contracts")
			return inference_connection_cases(argv[2], argv[3]);
		if (argc > 1 && (std::string(argv[1]) == "--officers" || std::string(argv[1]) == "--officer-snapshots"))
			return officer_journey(argc, argv);
		auto source = phoenix::test::controller_model_fixture();
		const int controllers = argc > 4 ? std::clamp(std::stoi(argv[4]), 1, 6) : 1;
		auto &ship = source["entities"][0];
		for (int i = 1; i < controllers; ++i)
		{
			const auto name = "propulsion_" + std::to_string(i);
			const auto engine = "engine_" + std::to_string(i);
			ship["systems"][engine] = ship["systems"]["port"];
			ship["stations"][0]["consoles"][0]["feeds"].push_back(engine);
			auto owner = ship["control_layout"]["controllers"][0];
			owner["id"] = name;
			owner["commands"][0]["consumers"] = Json::array({engine});
			ship["control_layout"]["controllers"].push_back(owner);
			ship["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back(name);
			ship["control_layout"]["profiles"][1]["consoles"]["drive"]["ai"].push_back(name);
		}
		const auto catalog = phoenix::test::load(source);
		World world;
		world.load(catalog, "test.map");
		world.tick();
		world.configure_controller_provider({1, "drive", "propulsion"}, "anyjev");
		for (int i = 1; i < controllers; ++i)
			world.configure_controller_provider({1, "drive", "propulsion_" + std::to_string(i)}, "anyjev");
		if (argc == 2 && std::string(argv[1]) == "--snapshot")
		{
			std::cout << world.controller_decision_snapshot({1, "drive", "propulsion"}).dump() << '\n';
			return 0;
		}
		if (argc < 3)
			throw std::runtime_error("Usage: probe --snapshot OR probe <pair-token> <service-url> [seconds]");
		const int seconds = argc > 3 ? std::stoi(argv[3]) : 30;
		InferenceHost host;
		host.set_connection(std::make_shared<InferenceConnection>(argv[1], argv[2]));
		const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
		auto next_tick = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() < end)
		{
			host.poll(world, 1,
			          [&](const Json &frame)
			          {
				          return world.submit_controller_decision(frame.at("decision"));
			          });
			if (std::chrono::steady_clock::now() >= next_tick)
			{
				world.tick();
				next_tick += std::chrono::milliseconds(50);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		const auto saved = world.save().at("bodies")[0].at("console_controllers").at("controllers")[0];
		auto connection = host.status();
		connection.erase("pair");
		std::cout << Json{{"provider", world.controller_ai(1)},
		                  {"history", saved.at("model").at("history")},
		                  {"commands", saved.at("commands")},
		                  {"connection", connection}}
		                 .dump()
		          << '\n';
		return saved.at("model").at("history").empty() ? 1 : 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
