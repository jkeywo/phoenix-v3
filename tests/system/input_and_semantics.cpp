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
		{
			InputRouter sidebar;
			require(sidebar.set_regions({{30, 0, 0, 1440, 1080}, {31, 1440, 0, 480, 1080}}),
			        "Viewscreen and right-quarter sidebar have distinct non-overlapping routes");
			require(sidebar.select_keyboard(30), "Viewscreen retains explicit keyboard selection");
			const auto touch = sidebar.pointer_down(7, 1500, 200);
			require(touch && touch->console == 31 && touch->x == 60 && sidebar.keyboard_console() == 30,
			        "sidebar touch is local and does not take Viewscreen keyboard focus");
			const auto drag = sidebar.pointer_move(7, 100, 200);
			require(drag && drag->console == 31, "sidebar keeps captured touch across the scene boundary");
			sidebar.pointer_up(7, 100, 200);
			sidebar.assign_gamepad("sidebar-pad", 31);
			sidebar.gamepad("sidebar-pad", "throttle", .5f);
			sidebar.drain_gamepad_inputs();
			sidebar.assign_gamepad("sidebar-pad", {});
			const auto releases = sidebar.drain_gamepad_inputs();
			require(releases.size() == 1 && releases.front().console == 31 && releases.front().value == 0,
			        "hiding sidebar neutralises its routed controller");
			require(sidebar.set_regions({{30, 0, 0, 1920, 1080}}), "closing sidebar restores full Viewscreen routing");
		}
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
		router.gamepad("pad0", "flight.yaw", -.8f);
		router.gamepad("pad0", "flight.thrust", 0);
		require(router.drain_gamepad_inputs().empty(), "a deflected stick cannot inherit a new Console assignment");
		router.gamepad("pad0", "flight.yaw", 0);
		(void)router.drain_gamepad_inputs();
		router.gamepad("pad0", "flight.yaw", -.575f);
		router.gamepad("pad0", "flight.thrust", .1f);
		inputs = router.drain_gamepad_inputs();
		require(inputs.size() == 1 && inputs[0].device == "pad0" && inputs[0].value < -.49f && inputs[0].value > -.51f,
		        "signed analogue axes retain source identity and rescale the shared dead zone");
		router.disconnect_gamepad("pad0");
		inputs = router.drain_gamepad_inputs();
		require(std::any_of(inputs.begin(), inputs.end(),
		                    [](const auto &i)
		                    {
			                    return i.control == "flight.yaw" && i.value == 0;
		                    }),
		        "analogue disconnect must deliver neutral through the ordinary route");
		router.gamepad("pad1", "throttle", 1);
		require(router.drain_gamepad_inputs().empty(), "disconnected/unassigned input must not claim a station");
		require(!router.set_regions({{11, 0, 0, 10, 10}, {22, 5, 5, 10, 10}}),
		        "overlapping console regions must be rejected atomically");
		require(router.pointer_move(102, 2000, 10)->console == 22,
		        "invalid layout must not discard an existing capture");
		Application client;
		require(client.music_volume == 50 && !invoke_navigation(client, "settings"),
		        "music settings start at half volume and remain server-only");
		Application host;
		int clicks = 0;
		host.button_click = [&]
		{
			++clicks;
		};
		host.can_host = true;
		host.screen = Screen::console;
		require(invoke_navigation(host, "settings") && host.screen == Screen::settings,
		        "the server can open settings during a mission");
		require(clicks == 1 && !invoke_navigation(host, "unknown-button") && clicks == 1,
		        "Shared button activation emits one local click and ignores unknown actions");
		require(host.master_volume == 50 && host.sfx_volume == 50 && set_audio_volume(host, "master", "0") &&
		            set_audio_volume(host, "sfx", "73") && host.master_volume == 0 && host.sfx_volume == 73 &&
		            !set_audio_volume(host, "master", "101") && !set_audio_volume(host, "sfx", "nan") &&
		            !set_audio_volume(host, "unknown", "10") && clicks == 1,
		        "Master and SFX sliders accept bounded independent levels without producing button clicks");
		require(set_music_volume(host, "0") && host.music_volume == 0 && set_music_volume(host, "100.000000") &&
		            host.music_volume == 100 && !set_music_volume(host, "nan") && !set_music_volume(host, "101") &&
		            !set_music_volume(host, "50junk") && !set_music_volume(client, "20"),
		        "slider and accessible volume edits share bounded numeric admission, including mute");
		host.screen_inputs = {{"music-volume", "Music volume", "100", true}};
		require(navigation_controls(host).back().percentage,
		        "music volume must publish range semantics rather than an unlabelled control");
		host.screen_controls = settings_tabs(host);
		host.screen_action = [](const std::string &)
		{
			return true;
		};
		require(!invoke_navigation(host, "menu.avatar") && !invoke_navigation(host, "menu.save-game"),
		        "Stale callbacks from inactive pages cannot upload a profile or save through Audio");
		host.screen_action = {};
		const auto tabs = navigation_controls(host);
		require(std::count_if(tabs.begin(), tabs.end(),
		                      [](const auto &control)
		                      {
			                      return control.tab && control.selected;
		                      }) == 1 &&
		            !invoke_navigation(host, "settings.tab/session") &&
		            !invoke_navigation(host, "settings.tab/unknown") &&
		            invoke_navigation(host, "settings.tab/display") && host.settings_tab == "display" &&
		            !set_audio_volume(host, "sfx", "10") && host.sfx_volume == 73,
		        "Settings tabs publish one selection, reject unavailable tabs and prevent hidden slider edits");
		require(invoke_navigation(host, "settings.dust") && !host.dust_motes &&
		            invoke_navigation(host, "settings.grid") && !host.reference_grid &&
		            invoke_navigation(host, "settings.motion") && host.reduced_motion &&
		            !invoke_navigation(client, "settings.dust"),
		        "Display settings use the same admitted semantic actions and retain independent preferences");
		require(invoke_navigation(host, "settings.close") && host.screen == Screen::console && host.music_volume == 100,
		        "closing settings returns to the prior screen and retains volume");
		require(!invoke_navigation(host, "settings.grid") && !host.reference_grid,
		        "Display preferences cannot change through an inactive Settings control");
		require(!set_audio_volume(host, "sfx", "50") && host.sfx_volume == 73,
		        "Mixer edits cannot change preferences through a closed Settings popup");
		require(!invoke_navigation(host, "settings.tab/audio"),
		        "Closed Settings cannot change tabs through stale actions");
		require(!invoke_navigation(client, "host") && !invoke_navigation(client, "edit"),
		        "semantic actions must preserve role restrictions");
		require(navigation_controls(client).size() == 4, "client semantics must not expose host/editor actions");
		require(invoke_navigation(client, "join") && client.screen == Screen::client_lobby,
		        "semantic action must use production navigation");
		invoke_navigation(client, "locale");
		require(navigation_controls(client).front().label == translate("home", true),
		        "graphics and semantics must share translated names");
		client.console_surface_active = true;
		client.screen_controls = {{"console/11/game.faster", "Increase speed"},
		                          {"console/22/game.contacts", "Read radar contacts"}};
		client.control_placements["console/22/game.contacts"] = {1920, 100, 300, 40, true, true};
		const auto semantics = navigation_controls(client);
		require(semantics.size() == 2 && semantics[1].placement.x == 1920 && semantics[1].placement.focused,
		        "Console semantics must retain per-region geometry and focus without unrelated global navigation");
		std::cout << "PASS console routing, safe release, semantic admission and translation\n";
		configure_translations(
		    Json{{"schema", 1}, {"languages", {{"en", {{"home", "Home"}}}, {"test", {{"home", "Translated home"}}}}}});
		select_language("test");
		require(translate("home", false) == "Translated home",
		        "UI and semantics must resolve the selected language resource");
		client.screen_inputs = {{"join-code", "Network code", "room/server"}};
		const auto with_input = navigation_controls(client);
		require(with_input.back().editable && with_input.back().value == "room/server",
		        "Editable semantic controls must expose their current text separately from their names");
		select_language("en");
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
