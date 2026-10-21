#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include "../../src/presentation/ship_camera.hpp"
#include <algorithm>
#include <iostream>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <vector>

import phoenix.simulation;
import phoenix.console;
import phoenix.session.local;
#include "../../src/network/projection.hpp"

using namespace phoenix;
using namespace phoenix::test;
namespace
{
void widgets(Json &source)
{
	source["consoles"][0]["widgets"] = Json::array({{{"id", "view-mode"},
	                                                 {"kind", "camera_mode"},
	                                                 {"binding", "ship.camera.mode"},
	                                                 {"label", "Camera mode"},
	                                                 {"x", 0},
	                                                 {"y", 0},
	                                                 {"width", 400},
	                                                 {"height", 200}},
	                                                {{"id", "view-direction"},
	                                                 {"kind", "camera_direction"},
	                                                 {"binding", "ship.camera.direction"},
	                                                 {"label", "Camera direction"},
	                                                 {"x", 0},
	                                                 {"y", 200},
	                                                 {"width", 400},
	                                                 {"height", 400}}});
}
void near(double actual, double expected)
{
	require(std::abs(actual - expected) < .000001, "camera geometry retains its selected bearing and ship anchor");
}
} // namespace
int main()
{
	try
	{
		auto source = controller_fixture();
		widgets(source);
		auto plain = source["consoles"][0];
		plain["id"] = "test.plain";
		plain["widgets"] = Json::array();
		source["consoles"].push_back(plain);
		source["entities"][0]["stations"][0]["consoles"].push_back(
		    {{"id", "plain"}, {"definition", "test.plain"}, {"systems", Json::array()}});
		source["maps"][0]["entities"].push_back(
		    {{"id", 2}, {"definition", "test.controllable"}, {"x_mm", 200000}, {"y_mm", 0}, {"heading_mdeg", 0}});
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map");
		require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer") && world.claim(103, 2, "drive") &&
		            world.claim(104, 1, "plain"),
		        "independent Console claims");
		require(ship_camera_state(world.public_board(1)) == ShipCameraState{},
		        "new ships start forward in first person");
		require(!world.submit(999, {1, "camera_mode", 1, {}}) && !world.submit(103, {1, "camera_mode", 1, {}}) &&
		            !world.submit(104, {1, "camera_mode", 1, {}}),
		        "claim, ship and authored capability are enforced");
		for (const auto value : {-1, 2, 8})
			require(!world.submit(101, {1, "camera_mode", value, {}}), "mode values are bounded");
		require(!world.submit(101, {1, "camera_direction", 8, {}}) &&
		            !world.submit(101, {1, "camera_direction", -1, {}}) &&
		            !world.submit(101, {1, "camera_mode", 1, "port"}),
		        "direction and physical binding validation");
		require(world.submit(101, {1, "camera_mode", 1, {}}) && world.submit(102, {1, "camera_direction", 2, {}}),
		        "two Consoles can change separate shared fields");
		require(ship_camera_state(world.public_board(1)).mode == 0,
		        "uncommitted requests are not rendered as accepted state");
		world.tick();
		require(ship_camera_state(world.public_board(1)) == ShipCameraState{1, 2} &&
		            ship_camera_state(world.observe(101)->gameplay) == ShipCameraState{1, 2} &&
		            ship_camera_state(world.observe(102)->gameplay) == ShipCameraState{1, 2} &&
		            ship_camera_state(world.public_board(2)) == ShipCameraState{},
		        "shared state stays on its ship");
		require(world.submit(101, {1, "camera_direction", 5, {}}) && world.submit(102, {1, "camera_direction", 7, {}}),
		        "conflicting changes enter ordinary ordered execution");
		world.tick();
		require(ship_camera_state(world.public_board(1)) == ShipCameraState{1, 7}, "last accepted field update wins");
		const auto saved = world.save();
		World resumed;
		resumed.restore(catalog, saved);
		require(ship_camera_state(resumed.public_board(1)) == ShipCameraState{1, 7},
		        "save restore retains accepted camera state");
		world.clear_claims();
		require(ship_camera_state(world.public_board(1)) == ShipCameraState{1, 7},
		        "handover does not reset the ship camera");
		// Runtime schema 12 rejects altered/retired knowledge contracts.
		auto legacy = saved;
		auto &runtime = legacy["knowledge"]["runtime"];
		const auto &schemas = runtime["contract"]["schemas"];
		const auto camera_schema =
		    static_cast<std::size_t>(std::find_if(schemas.begin(), schemas.end(),
		                                          [](const auto &schema)
		                                          {
			                                          return schema.at("name") == "phoenix.camera";
		                                          }) -
		                             schemas.begin()) +
		    1;
		runtime["contract"]["schemas"].erase(camera_schema - 1);
		std::erase_if(runtime["records"].get_ref<Json::array_t &>(),
		              [&](const auto &row)
		              {
			              return row.at("value").at("schema") == camera_schema;
		              });
		rejects(
		    [&]
		    {
			    resumed.restore(catalog, legacy);
		    },
		    "incompatible knowledge contracts require a new session");
		auto invalid = saved;
		for (auto &row : invalid["knowledge"]["runtime"]["records"])
			if (row["value"]["schema"] == camera_schema)
				row["value"]["fields"][0]["value"] = 9;
		rejects(
		    [&]
		    {
			    resumed.restore(catalog, invalid);
		    },
		    "malformed saved camera values fail validation");
		require(ship_camera_state(resumed.public_board(1)) == ShipCameraState{1, 7}, "failed restore is transactional");
		// Network projection must retain the live camera even when reports exhaust the budget.
		auto board = world.public_board(1);
		for (int i = 0; i < 150; ++i)
			board["shared"].push_back({{"schema", "test.report"}, {"fields", {{"text", std::string(10000, 'x')}}}});
		require(ship_camera_state(network::public_board_json(board)) == ShipCameraState{1, 7},
		        "bounded public projection retains camera settings");
		for (int direction = 0; direction < 8; ++direction)
			for (int mode = 0; mode < 2; ++mode)
			{
				const auto pose = ship_camera_pose(10000, 20000, 359000, 10, 2, mode, direction);
				const double dx = std::cos(pose.bearing), dz = -std::sin(pose.bearing);
				if (mode)
				{
					near(pose.target[0], 10);
					near(pose.target[2], -20);
					near(pose.position[0], 10 - dx * 60);
				}
				else
				{
					near(pose.position[0], 10 - dx * 5);
					near(pose.position[2], -20 - dz * 5);
					near(pose.target[0], 10 + dx * 11);
					near(pose.target[2], -20 + dz * 11);
				}
				const auto wrapped = ship_camera_pose(10000, 20000, -1000, 10, 2, mode, direction);
				near(pose.position[0], wrapped.position[0]);
				near(pose.target[2], wrapped.target[2]);
			}
		auto session_source = controller_session_fixture();
		widgets(session_source);
		auto local = make_local_session();
		local->add_pack(archive(session_source));
		local->start({"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "drive"});
		ConsoleModel model;
		model.refresh(local);
		require(model.command("camera_mode", 1) && model.command("camera_direction", 4),
		        "ConsoleModel submits camera controls through SessionPort");
		local->tick();
		model.refresh(local);
		require(model.camera() == ShipCameraState{1, 4}, "ConsoleModel reflects accepted state");
		auto recovered = make_local_session();
		recovered->restore(local->save());
		recovered->resume_local();
		require(ship_camera_state(recovered->public_board()) == ShipCameraState{1, 4},
		        "portable session recovery retains camera state");
		for (const auto *field : {"system", "controller", "channel"})
		{
			auto invalid_content = source;
			invalid_content["consoles"][0]["widgets"][0][field] = "port";
			rejects(
			    [&]
			    {
				    load(invalid_content);
			    },
			    "camera widgets cannot bind physical mechanisms");
		}
		std::cout
		    << "PASS ship camera commands, shared projections, geometry, admission, save compatibility and recovery\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
