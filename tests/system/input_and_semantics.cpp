#include <phoenix/input.hpp>
#include <phoenix/semantics.hpp>
#include <iostream>
#include <stdexcept>
using namespace phoenix;
void require(bool value, const char *contract)
{
	if (!value)
		throw std::runtime_error(contract);
}
int main()
{
	try
	{
		InputRouter router;
		require(router.set_regions({{11, 0, 0, 1920, 1080},
		                            {22, 1920, 0, 1920, 1080},
		                            {33, 0, 1080, 1920, 1080},
		                            {44, 1920, 1080, 1920, 1080},
		                            {55, 3840, 1080, 1920, 1080}}),
		        "arbitrary five-console layout must load");
		require(router.select_keyboard(22), "explicit keyboard assignment must succeed");
		auto first = router.pointer_down(101, 10, 10);
		auto second = router.pointer_down(102, 2000, 10);
		require(first && second && first->console == 11 && second->console == 22 && second->x == 80,
		        "simultaneous touch contacts must route independently in local coordinates");
		auto crossed = router.pointer_move(101, 2100, 20);
		require(crossed && crossed->console == 11, "pointer capture must survive crossing another console");
		require(router.keyboard_console() == 22, "touch must not redirect keyboard/accessibility focus");
		require(router.pointer_up(101, 2100, 20)->released, "pointer release must reach captured console");
		require(!router.pointer_move(101, 20, 20), "released contact must lose capture");
		for (unsigned i = 0; i < 5; ++i)
		{
			router.assign_gamepad("pad" + std::to_string(i), (i + 1) * 11);
			router.gamepad("pad" + std::to_string(i), "throttle", 0.75f);
		}
		auto inputs = router.drain_gamepad_inputs();
		require(inputs.size() == 5 && inputs.back().console == 55,
		        "routing contract must support five independently assigned controllers");
		router.assign_gamepad("pad0", 22);
		router.disconnect_gamepad("pad1");
		inputs = router.drain_gamepad_inputs();
		require(inputs.size() == 2 && inputs[0].console == 11 && inputs[0].value == 0 && inputs[1].console == 22 &&
		            inputs[1].value == 0,
		        "reassignment/disconnect must neutralise the old console");
		router.gamepad("pad1", "throttle", 1);
		require(router.drain_gamepad_inputs().empty(), "disconnected/unassigned input must not claim a station");
		require(!router.set_regions({{11, 0, 0, 10, 10}, {22, 5, 5, 10, 10}}),
		        "overlapping console regions must be rejected atomically");
		require(router.pointer_move(102, 2000, 10)->console == 22,
		        "invalid layout must not discard an existing capture");
		Application client;
		require(!invoke_navigation(client, "host") && !invoke_navigation(client, "edit"),
		        "semantic actions must preserve role restrictions");
		require(navigation_controls(client).size() == 4, "client semantics must not expose host/editor actions");
		require(invoke_navigation(client, "join") && client.screen == Screen::client_lobby,
		        "semantic action must use production navigation");
		invoke_navigation(client, "locale");
		require(navigation_controls(client).front().label == translate("home", true),
		        "graphics and semantics must share translated names");
		std::cout << "PASS console routing, safe release, semantic admission and translation\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
