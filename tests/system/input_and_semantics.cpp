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

import phoenix.content.verification;
import phoenix.ui.interaction;
import phoenix.input;
import phoenix.navigation;
import phoenix.console;
import phoenix.knowledge.read_models;

using namespace phoenix;
using phoenix::test::require;
struct InferenceView final : SessionPort
{
	Json state = Json::object();
	Catalog content;
	std::optional<Observation> projection;
	std::vector<OperatingCommand> submitted;
	void add_pack(const Bytes &) override
	{
	}
	void clear_packs() override
	{
	}
	const Catalog &catalog() const override
	{
		return content;
	}
	void start(const GameSetup &) override
	{
	}
	bool running() const override
	{
		return false;
	}
	bool recovery_pending() const override
	{
		return false;
	}
	Bytes save() const override
	{
		return {};
	}
	void restore(const Bytes &) override
	{
	}
	void resume_local() override
	{
	}
	void tick() override
	{
	}
	std::uint64_t ticks() const override
	{
		return 0;
	}
	bool command(const OperatingCommand &value) override
	{
		if (!projection)
			return false;
		submitted.push_back(value);
		return true;
	}
	std::optional<Observation> observation() const override
	{
		return projection;
	}
	std::vector<BodyView> viewscreen() const override
	{
		return {};
	}
	Json inference_state() const override
	{
		return state;
	}
};
int main()
{
	try
	{
#ifndef __EMSCRIPTEN__
		{
			Application devices;
			devices.screen_identities = {{0, "monitor-host"}, {1, "monitor-a"}, {2, "monitor-b"}};
			devices.gamepad_devices = {{"7", {"Pad", "serial-a", "path-a", "model"}},
			                           {"8", {"Pad", "serial-b", "path-b", "model"}}};
			require(assign_screen_gamepad(devices, 1, "7") && assign_screen_gamepad(devices, 2, "8"),
			        "Screens independently assign controllers");
			require(devices.gamepad_assignments.at("7") == 1 && devices.gamepad_assignments.at("8") == 2,
			        "Controller routes remain independent");
			devices.screen_identities = {{0, "monitor-host"}, {4, "monitor-b"}, {5, "monitor-a"}};
			restore_screen_gamepads(devices);
			require(devices.gamepad_assignments.at("7") == 5 && devices.gamepad_assignments.at("8") == 4,
			        "Monitor identities survive route and Console changes");
			devices.gamepad_devices.erase("7");
			devices.paired_gamepads.clear();
			restore_screen_gamepads(devices);
			require(!devices.gamepad_assignments.contains("7") &&
			            !devices.screen_devices.at("monitor-a").gamepad_serial.empty(),
			        "Disconnect retains remembered assignment");
			devices.gamepad_devices["17"] = {"Pad", "serial-a", "path-a", "model"};
			restore_screen_gamepads(devices);
			require(devices.gamepad_assignments.at("17") == 5 && devices.gamepad_wait_neutral.contains("17"),
			        "Reconnect uses persistent identity and waits for neutral");
			devices.gamepad_devices["18"] = devices.gamepad_devices.at("17");
			restore_screen_gamepads(devices);
			require(!devices.gamepad_assignments.contains("17") && !devices.gamepad_assignments.contains("18"),
			        "Ambiguous hardware cannot silently acquire a screen");
			devices.gamepad_devices.erase("18");
			require(assign_screen_gamepad(devices, 4, "17") &&
			            devices.screen_devices.at("monitor-a").gamepad_name.empty(),
			        "Explicit reassignment moves ownership from the previous screen");
			devices.pairing_screen = "monitor-a";
			require(screen_gamepad_event(devices, "17", "game.contacts", 1) &&
			            devices.gamepad_assignments.at("17") == 5,
			        "Pairing consumes the identifying press");
			require(screen_gamepad_event(devices, "17", "flight.yaw", .8f) &&
			            screen_gamepad_event(devices, "17", "game.contacts", 0) &&
			            devices.gamepad_wait_neutral.contains("17"),
			        "Held axes prevent premature pairing activation");
			require(screen_gamepad_event(devices, "17", "flight.yaw", 0) &&
			            screen_gamepad_event(devices, "17", "device.neutral", 0) &&
			            !devices.gamepad_wait_neutral.contains("17"),
			        "Controller resumes only after neutral");
			devices.audio_outputs = {{"speaker-a", "Speakers A"}, {"speaker-b", "Speakers B"}};
			require(screen_device_action(devices, 4, "device.audio/speaker-a") &&
			            screen_device_action(devices, 5, "device.audio/speaker-b"),
			        "Audio outputs are selected per screen");
			devices.audio_outputs.clear();
			require(screen_audio_output(devices, 4) == "speaker-a" && screen_audio_output(devices, 5) == "speaker-b",
			        "Missing saved outputs remain selected rather than falling back");
			require(screen_audio_output(devices, 0) == "default" &&
			            screen_device_action(devices, 5, "device.audio/muted") &&
			            screen_audio_output(devices, 5) == "muted",
			        "New screens default to system output and can explicitly mute");
			devices.gamepad_devices["20"] = {"Anonymous pad", "", "stable-path", "other-model"};
			require(assign_screen_gamepad(devices, 4, "20"), "Path-only devices can be paired explicitly");
			devices.paired_gamepads.clear();
			devices.gamepad_devices.erase("20");
			devices.gamepad_devices["21"] = {"Anonymous pad", "", "stable-path", "other-model"};
			restore_screen_gamepads(devices);
			require(devices.gamepad_assignments.at("21") == 4, "A unique stable path restores without a serial");
			devices.screen_devices["monitor-a"] = devices.screen_devices.at("monitor-b");
			restore_screen_gamepads(devices);
			require(!devices.gamepad_assignments.contains("21"), "Conflicting remembered identities require pairing");
			devices.screen_identities = {{0, "single-monitor"}, {1, "single-monitor"}, {31, "single-monitor"}};
			devices.screen = Screen::console;
			require(assign_screen_gamepad(devices, 1, "21") && devices.gamepad_assignments.at("21") == 1,
			        "A standalone Console receives its screen controller instead of the host alias");
		}
#endif

		{
			const Json row{{"id", "equipment:engine:cooling"},
			               {"rule", "test.cooling"},
			               {"label", "rule.label"},
			               {"source_kind", "equipment"},
			               {"equipment", "engine"},
			               {"phase", "active"},
			               {"output", 750},
			               {"blocker", ""}};
			auto extra = row;
			extra["target"] = 999;
			extra["facts"] = {{"hidden", 42}};
			extra["origin"] = "hidden-source";
			require(rules_projection::board(Json::array({extra})) == Json::array({row}),
			        "Rule publication retains only public metadata, never captured targets, facts or provenance");
			for (const auto &field : {"phase", "output", "id", "blocker", "source_kind"})
			{
				auto invalid = row;
				if (std::string(field) == "phase")
					invalid[field] = "secret-phase";
				else if (std::string(field) == "output")
					invalid[field] = 1001;
				else if (std::string(field) == "blocker")
					invalid[field] = "Secret target failed its private condition";
				else if (std::string(field) == "source_kind")
					invalid[field] = "secret-kind";
				else
					invalid[field] = "";
				bool refused = false;
				try
				{
					(void)rules_projection::board(Json::array({invalid}));
				}
				catch (const std::exception &)
				{
					refused = true;
				}
				require(refused, "Malformed rule status is refused before publication");
			}
			require(rules_projection::board(Json::array()).empty(), "No rule rows require no placeholder status");
		}
		{
			auto session = std::make_shared<InferenceView>();
			PackDraft pack;
			pack.manifest = {{"schema", 1}, {"id", "ui-routing"}};
			pack.files["assets/pixel.png"] = source_asset_image(AssetImage{1, 1, {255, 255, 255, 255}});
			Json definitions{
			    {"schema", 1},
			    {"consoles", Json::array({{{"id", "ui.first"}, {"widgets", Json::array()}},
			                              {{"id", "ui.second"}, {"widgets", Json::array()}}})},
			    {"entities",
			     Json::array(
			         {{{"id", "ui.ship"},
			           {"render", {{"mode", "camera"}, {"scale", 1}, {"images", {"ui-routing:assets/pixel.png"}}}},
			           {"systems", {{"engine", {{"max_speed_mm_s", 17000}, {"acceleration_mm_s2", 2000}}}}},
			           {"collision", {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 10000}}},
			           {"stations",
			            Json::array(
			                {{{"id", "watch"},
			                  {"consoles",
			                   Json::array(
			                       {{{"id", "a"}, {"definition", "ui.first"}, {"systems", {"engine"}}},
			                        {{"id", "b"}, {"definition", "ui.second"}, {"systems", Json::array()}}})}}})}}})}};
			definitions["control_widgets"] = Json::parse(
			    R"json([{"id":"ui.engine.widget","schema":1,"fallback":"hold","equipment":{"engine":{}},"commands":[{"id":"speed","action":"speed_mm_s","consumers":["$engine"]},{"id":"thrust","action":"longitudinal_thrust","consumers":["$engine"]}],"levels":[{"id":"automated","human":[]},{"id":"manual","human":["speed","thrust"]}]}])json");
			definitions["entities"][0]["motion_model"] = "inertial";
			definitions["entities"][0]["control_layout"] = Json::parse(
			    R"json({"schema":2,"default_profile":"manual","controllers":[{"id":"engine","console":"a","widget":"ui.engine.widget","equipment":{"engine":"engine"},"star_levels":{"1":"manual","2":"manual","3":"manual"}}],"profiles":[{"id":"manual","stars":3}]})json");
			pack.files["definitions.json"] = json_bytes(definitions);
			session->content = resolve_packs({read_pack(write_pack(pack))});
			Observation first;
			first.own = {1, "ui.ship", 0, 0, 0, 0, 0, RadarIcon::ship};
			first.console = "a";
			first.controls = first.feeds = system_bit(OperatingSystem::Engine);
			first.systems = {{"engine", OperatingSystem::Engine, 0, 17000, true, true}};
			first.gameplay["controllers"] = Json::parse(
			    R"json([{"id":"engine","console":"a","widget":"ui.engine.widget","generation":1,"human":true,"commands":{"thrust":{"action":"longitudinal_thrust","human":true,"minimum":-1000,"maximum":1000,"value":0,"semantics":"setting","consumers":["engine"]}}}])json");
			first.gameplay["controls"] = Json::array({{{"id", "axis"},
			                                           {"target", 1},
			                                           {"label", "Forward thrust"},
			                                           {"action", "longitudinal_thrust"},
			                                           {"system", ""},
			                                           {"controller", "engine"},
			                                           {"channel", "thrust"},
			                                           {"generation", 1},
			                                           {"value", 1000}}});
			auto second = first;
			second.console = "b";
			second.controls = 0;
			second.systems[0].controlled = false;
			second.gameplay["controls"] = Json::array();
			second.gameplay["controllers"] = Json::array();
			second.gameplay["conversations"] = {{"threads", Json::array({{{"unread", true}}})}};
			first.additional_consoles = {second};
			session->projection = first;
			ConsoleModel model;
			model.refresh(session);
			ConsoleModel independent_surface;
			independent_surface.refresh(session);
			require(model.available_consoles().size() == 2 && !model.act("game.console/foreign"),
			        "Console tabs use only granted authored Consoles");
			require(model.available_tabs()[1].attention, "inactive tab attention comes from its permitted observation");
			require(model.hold_control("game.control/61786973"),
			        "synthetic permitted Console accepts a held descriptor");
			require(model.act("game.console/b") && session->submitted.back().console == "a" &&
			            session->submitted.back().value == 0,
			        "switching releases the original Console held input");
			model.refresh(session);
			require(model.observation()->console == "b" && !model.command("longitudinal_thrust", 1000, "engine"),
			        "read-only Console tab cannot inherit another Console authority");
			independent_surface.refresh(session);
			require(independent_surface.observation()->console == "a",
			        "switching one player surface preserves another surface Console");
			session->projection->additional_consoles.clear();
			model.refresh(session);
			require(model.available_consoles().size() == 1 && model.observation()->console == "a",
			        "revoked tab disappears and returns to the remaining Console");
			configure_translations(
			    Json{{"schema", 1},
			         {"languages",
			          {{"en", {{"ability.label", "Emergency shield"}, {"Activate ability", "Activate ability"}}},
			           {"test",
			            {{"ability.label", "Bouclier"},
			             {"Activate ability", "Activer"},
			             {"rule.label", "Refroidissement"},
			             {"paused", "En pause"},
			             {"Rule output", "Puissance"},
			             {"Rule funding unavailable", "Financement indisponible"}}}}}});
			select_language("test");
			session->projection->gameplay["controls"].push_back({{"id", "ability-localisation"},
			                                                     {"label", "Activate ability"},
			                                                     {"ability_label", "ability.label"},
			                                                     {"ability_target", 1},
			                                                     {"action", "system_enabled"},
			                                                     {"system", "engine"},
			                                                     {"value", 1}});
			model.refresh(session);
			const auto translated_controls = model.gameplay_controls();
			require(std::any_of(translated_controls.begin(), translated_controls.end(),
			                    [](const auto &control)
			                    {
				                    return control.second == "Bouclier [1]: Activer";
			                    }),
			        "Ability controls translate their label key before formatting the target and action");
			session->projection->gameplay["rules"] = Json::array({{{"id", "rule-localisation"},
			                                                       {"rule", "test.cooling"},
			                                                       {"label", "rule.label"},
			                                                       {"source_kind", "equipment"},
			                                                       {"equipment", "engine"},
			                                                       {"phase", "paused"},
			                                                       {"output", 750},
			                                                       {"blocker", "funding"}}});
			model.refresh(session);
			const auto rule_rows = model.gameplay_readouts();
			require(std::any_of(rule_rows.begin(), rule_rows.end(),
			                    [](const auto &row)
			                    {
				                    return row.first.starts_with("game.rule/") &&
				                           row.second ==
				                               "Refroidissement; En pause; Puissance 75%; Financement indisponible";
			                    }),
			        "Automatic rule readouts localize authored labels, phases, output and generic blockers");
			require(model.gameplay_controls() == translated_controls,
			        "Automatic rule status does not create operation controls");
			select_language("en");
		}
		{
			Application app;
			app.can_host = true;
			app.screen = Screen::viewscreen;
			auto view = std::make_shared<InferenceView>();
			app.session = view;
			require(!ai_configuration_available(app) && !invoke_navigation(app, "ai.config"),
			        "unconfigured inference hides and refuses the AI button");
			const auto controls = navigation_controls(app);
			require(std::none_of(controls.begin(), controls.end(),
			                     [](const auto &c)
			                     {
				                     return c.id == "ai.config";
			                     }),
			        "unconfigured button absent from accessibility navigation");
			view->state = {{"configured", true}, {"online", false}, {"providers", {"anyjev"}}};
			require(ai_configuration_available(app) && invoke_navigation(app, "ai.config") && app.settings_tab == "ai",
			        "configured offline provider retains repair access");
			app.can_host = false;
			require(!ai_configuration_available(app) && !invoke_navigation(app, "ai.pair"),
			        "crew cannot change host provider configuration");
		}
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
		Application persistence;
		complete_preference_persistence(persistence, "synthetic quota exceeded");
		require(persistence.preference_error.find("synthetic quota exceeded") != std::string::npos &&
		            persistence.status == persistence.preference_error &&
		            persistence.accessible_status == persistence.preference_error,
		        "Asynchronous settings failure reaches both visible and accessible status");
		persistence.preference_pending_error = persistence.preference_error;
		complete_preference_persistence(persistence, "");
		require(persistence.preference_error.empty() && persistence.status.empty() &&
		            persistence.accessible_status.empty(),
		        "Confirmed durable settings recovery clears its own diagnostic");
		persistence.preference_error = "newer synchronous write failed";
		persistence.status = persistence.accessible_status = "Mission ready";
		complete_preference_persistence(persistence, "");
		require(persistence.preference_error == "newer synchronous write failed" &&
		            persistence.status == "Mission ready" && persistence.accessible_status == "Mission ready",
		        "Periodic persistence cannot clear an unrelated settings failure or status");
		persistence.preference_pending_error = persistence.preference_error;
		complete_preference_persistence(persistence, "");
		require(persistence.preference_error.empty() && persistence.status == "Mission ready",
		        "A later successful settings rewrite clears the corresponding write failure");
		complete_preference_persistence(persistence, "synthetic display quota", PreferenceStorage::Display);
		const auto display_error = persistence.preference_error;
		complete_preference_persistence(persistence, "");
		require(persistence.preference_error == display_error && persistence.accessible_status == display_error,
		        "Menu persistence cannot clear a failed standalone display write");
		complete_preference_persistence(persistence, "synthetic menu quota");
		const auto menu_error = persistence.preference_error;
		complete_preference_persistence(persistence, "synthetic display retry", PreferenceStorage::Display);
		complete_preference_persistence(persistence, "", PreferenceStorage::Display);
		require(persistence.preference_error == menu_error && persistence.accessible_status == menu_error &&
		            persistence.display_preference_error.empty(),
		        "Display recovery restores an outstanding menu storage failure");
		complete_preference_persistence(persistence, "");
		require(persistence.preference_error.empty() && persistence.status.empty() &&
		            persistence.accessible_status.empty(),
		        "Independent display and menu recovery clear only their own diagnostics");
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
		require(navigation_controls(client, false).empty(),
		        "root-window placement queries exclude Console entries without changing full accessibility output");
		require(semantics.size() == 2 && semantics[1].placement.x == 1920 && semantics[1].placement.focused,
		        "Console semantics must retain per-region geometry and focus without unrelated global navigation");
		{
			PadNavigation pad, other;
			pad.sample("flight.yaw", 0, 0);
			pad.sample("flight.thrust", 0, 0);
			pad.sample("flight.yaw", .2f, 0);
			require(pad.drain(.1).empty(), "Menu stick drift does not become Tab");
			pad.sample("flight.yaw", .3f, .1);
			auto events = pad.drain(.1);
			require(events.size() == 1 && events[0].action == "left" && events[0].pressed,
			        "Navigation emits an immediate threshold crossing");
			require(pad.drain(.49).empty() && pad.drain(.5).size() == 1 && pad.drain(.55).empty() &&
			            pad.drain(.61).size() == 1 && other.drain(1).empty(),
			        "Navigation repeat is bounded and device state is independent");
			pad.sample("flight.yaw", .16f, .62);
			require(pad.drain(.62).empty(), "Navigation retains its direction within hysteresis");
			pad.sample("flight.yaw", 0, .63);
			events = pad.drain(.63);
			require(events.size() == 1 && !events.front().pressed, "Navigation releases below its release threshold");
			pad.sample("game.contacts", 1, .7);
			require(pad.drain(.7).size() == 1 && pad.drain(2).empty(), "Activation never repeats");
			pad.cancel();
			require(!pad.drain(2).front().pressed, "Cancellation pairs activation with release");
			pad.sample("game.contacts", 1, 2);
			require(pad.drain(2).empty(), "A held button cannot reactivate after interruption");
			pad.sample("game.contacts", 0, 2);
			pad.sample("game.contacts", 1, 2.1);
			require(pad.drain(2.1).size() == 1, "Released input can activate again after neutral recovery");
			pad.sample("game.contacts", 0, 2.2);
			pad.sample("game.faster", 1, 2.2);
			pad.sample("flight.thrust", 0, 2.2);
			pad.drain(2.2);
			require(pad.drain(2.61).size() == 1, "Neutral stick samples do not release a held D-pad direction");
		}

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
		require(translate("home", false) == "Translated home", "Warm translation reads preserve the selected resource");
		configure_translations(
		    Json{{"schema", 1},
		         {"languages", {{"en", {{"home", "Updated home"}}}, {"test", {{"home", "Updated translation"}}}}}});
		require(translate("home", false) == "Updated translation", "Replacing resources invalidates warm translations");
		select_language("en");
		require(translate("home", false) == "Updated home" && translate("home", true) == "[ Updated home ~~~ ]",
		        "Translation reuse must distinguish both locale and pseudo mode");
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
