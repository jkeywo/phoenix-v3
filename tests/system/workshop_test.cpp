#include <iostream>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import phoenix.workshop_test;

using namespace phoenix;
namespace
{
void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}
WorkshopTestRequest fixture()
{
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", "disposable-test"}};
	const Json definitions{
	    {"schema", 1},
	    {"entities",
	     Json::array(
	         {{{"id", "disposable.ship"},
	           {"render",
	            {{"mode", "camera"}, {"images", Json::array({"disposable-test:assets/pixel.png"})}, {"scale", 1}}},
	           {"systems",
	            {{"engine", {{"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}}},
	             {"steering", {{"turn_mdeg_s", 90000}}}}},
	           {"collision", {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 1000}}}}})},
	    {"maps",
	     Json::array({{{"id", "disposable.map"},
	                   {"entities", Json::array()},
	                   {"ship_slots", Json::array({{{"id", "bridge"},
	                                                {"x_mm", 0},
	                                                {"y_mm", 0},
	                                                {"heading_mdeg", 0},
	                                                {"allowed_ships", Json::array({"disposable.ship"})}}})}}})}};
	pack.files["definitions.json"] = json_bytes(definitions);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	WorkshopTestRequest result;
	result.pack = write_pack(pack);
	result.mission = "disposable.map";
	result.slot = "bridge";
	result.ship = "disposable.ship";
	return result;
}
WorkshopTestRequest operation(WorkshopTestAction action)
{
	WorkshopTestRequest request;
	request.action = action;
	return request;
}
} // namespace
int main()
{
	try
	{
		const auto snapshot = fixture();
		GameplayTestHost first, second;
		require(first.submit(snapshot),
		        "disposable test must construct an isolated World through production pack/load paths");
		require(second.submit(snapshot), "independent disposable hosts must support the same authored snapshot");
		first.advance(100);
		require(first.feedback().at("tick") == 2 && second.feedback().at("tick") == 0,
		        "advancing a disposable host must not tick another World");
		require(first.submit(operation(WorkshopTestAction::Pause)), "pause must address the existing disposable World");
		first.advance(250);
		require(first.feedback().at("tick") == 2, "paused disposable test must not advance");
		require(first.submit(operation(WorkshopTestAction::Step)) && first.feedback().at("tick") == 3,
		        "step must execute exactly one real production tick");
		first.advance(100);
		require(first.feedback().at("tick") == 3, "single step must leave the test paused");
		require(first.submit(operation(WorkshopTestAction::Pause)), "pause action toggles resume");
		first.advance(50);
		require(first.feedback().at("tick") == 4, "resumed test must advance from the retained state");
		auto invalid = snapshot;
		invalid.slot = "missing";
		require(!first.submit(std::move(invalid)) && first.feedback().at("tick") == 4,
		        "invalid replacement must report the slot error without replacing the current disposable World");
		require(first.submit(operation(WorkshopTestAction::Restart)) && first.feedback().at("tick") == 0,
		        "restart must rebuild the original exported snapshot rather than the failed replacement");
		require(first.submit(operation(WorkshopTestAction::Stop)) && !first.feedback().at("running").get<bool>(),
		        "stop must dispose of the isolated World");
		require(!first.submit(operation(WorkshopTestAction::Step)), "step without a running test must fail clearly");
		std::cout << "disposable Workshop integration passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
