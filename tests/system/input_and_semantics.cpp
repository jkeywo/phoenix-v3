#include "../support/assertions.hpp"
#include <iostream>
#include <cmath>
#include <iterator>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
#include "../../src/tools/map_gesture.hpp"
}

import phoenix.input;
import phoenix.navigation;

using namespace phoenix;
using phoenix::test::require;
int main()
{
	try
	{
		using P = Application::ToolPointer;
		MapGesture gesture;
		float pan_x = 0, pan_y = 0, zoom = 1;
		auto send = [&](P::Phase phase, std::uint64_t id, float x, float y, bool inside = true)
		{
			gesture.event(P{phase, id, x, y}, inside, 200, 200, .25f, 4.f, pan_x, pan_y, zoom);
		};
		send(P::down, 1, 100, 100);
		send(P::move, 1, 120, 110);
		require(pan_x == 20 && pan_y == 10 && zoom == 1, "One finger pans without changing scale");
		send(P::down, 2, 220, 110);
		const auto world_x = (170 - 200 - pan_x) / zoom;
		const auto world_y = (110 - 200 - pan_y) / zoom;
		send(P::move, 2, 320, 110);
		require(zoom == 2 && std::abs((220 - 200 - pan_x) / zoom - world_x) < .001f &&
		            std::abs((110 - 200 - pan_y) / zoom - world_y) < .001f,
		        "Pinch zoom preserves the world point beneath the moving midpoint");
		send(P::move, 2, 10000, 110);
		require(zoom == 4, "Pinching respects the map zoom limit");
		send(P::cancel, 1, 0, 0);
		const auto prior_pan = pan_x;
		send(P::move, 2, 10010, 110, false);
		require(pan_x == prior_pan + 10, "Lifting one finger resumes pan without a jump and keeps capture outside");
		send(P::up, 2, 10010, 110);
		send(P::down, 3, 20, 20, false);
		send(P::move, 3, 50, 50);
		require(gesture.contacts.empty() && pan_x == prior_pan + 10,
		        "Touches beginning outside the map never capture it");
		ManualFlightInput flight;
		flight.sample("keyboard", {1000, 1000});
		require(flight.axes() == FlightAxes{1000, 1000}, "yaw and thrust remain independent at full diagonal input");
		flight.sample("pad", {-500, -700});
		require(flight.axes() == FlightAxes{1000, 1000}, "a second held device cannot replace the active source");
		flight.sample("keyboard", {});
		flight.sample("pad", {-500, -700});
		require(flight.axes() == FlightAxes{}, "a competing held stick cannot inherit control on key release");
		flight.sample("pad", {});
		flight.sample("pad", {-500, -700});
		require(flight.axes() == FlightAxes{-500, -700},
		        "a fresh stick movement takes control with proportional signed axes");
		flight.cancel();
		flight.sample("pad", {-500, -700});
		require(flight.axes() == FlightAxes{},
		        "focus, mode and authority interruption require neutral before resuming");
		flight.sample("pad", {});
		flight.sample("pad", {200, 100});
		flight.cancel_source("keyboard");
		require(flight.axes() == FlightAxes{200, 100},
		        "keyboard selection changes do not interrupt an assigned gamepad");
		flight.cancel_source("pad");
		require(flight.axes() == FlightAxes{}, "source cancellation immediately neutralises both axes");
