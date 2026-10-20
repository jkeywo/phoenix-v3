#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include "../../src/presentation/navigation_camera.hpp"
#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

import phoenix.simulation;
import phoenix.console;
import phoenix.session.execution;
import phoenix.navigation;
#include "../../src/network/projection.hpp"
#include "../../src/presentation/alert_audio.hpp"

using namespace phoenix;
using namespace phoenix::test;

namespace
{
Json controller(const World &world, std::uint64_t claimant, const std::string &id)
{
	const auto observation = world.observe(claimant);
	require(observation.has_value(), "claimed Console has a permitted projection");
	for (const auto &value : observation->gameplay.at("controllers"))
		if (value.at("id") == id)
			return value;
	throw std::runtime_error("Controller absent from owning Console projection: " + id);
}
OperatingCommand speed(const World &world, std::uint64_t claimant, const std::string &id, std::int64_t value)
{
	OperatingCommand command{1, {}, value, {}};
	command.controller = id;
	command.channel = "speed";
	command.generation = controller(world, claimant, id).at("generation").get<std::uint64_t>();
	return command;
}
std::int64_t requested(const World &world, const std::string &id)
{
	const auto observation = world.observe(101);
	require(observation.has_value(), "physical observation has an owning Console");
	for (const auto &system : observation->systems)
		if (system.id == id)
			return system.requested;
	throw std::runtime_error("Bound physical observation absent: " + id);
}
} // namespace

int main()
{
	try
	{
		{
			auto source = controller_fixture();
			auto &ship = source["entities"][0];
			source["consoles"][0]["stances"] =
			    Json::array({{{"id", "routine"}, {"label", "Routine"}}, {{"id", "hold"}, {"label", "Hold"}}});
			source["consoles"].push_back({{"id", "test.command"},
			                              {"human_seeking", true},
			                              {"command", true},
			                              {"hide_without_ai", true},
			                              {"viewscreen", true},
			                              {"compact", {{"widgets", Json::array()}}},
			                              {"widgets", Json::array()}});
			ship["stations"][0]["consoles"].push_back(
			    {{"id", "command"}, {"definition", "test.command"}, {"systems", Json::array()}});
			ship["bridge"] = {
			    {"initial", "routine"},
			    {"alert_console", "observer"},
			    {"sidebar_default", "command"},
			    {"conditions",
			     Json::array({{{"id", "routine"}, {"label", "Routine"}, {"colour", "#001122"}},
			                  {{"id", "danger"}, {"label", "Danger"}, {"colour", "#ff0000"}, {"glow", true}},
			                  {{"id", "evacuate"}, {"label", "Evacuate"}}})}};
			source["trees"].push_back(Json::parse(R"({
				"id":"test.bridge-ai","context":"controller","initial":"routine",
				"command_bindings":[{"channel":"speed"}],
				"states":[
					{"id":"routine","running":[{"action":"write.speed","arguments":[1000]}],
					 "transitions":[{"target":"danger","conditions":[{"left":{"observation":"alert.id"},"op":"eq","right":"danger"},{"left":{"observation":"alert.severity"},"op":"eq","right":1}]}]},
					{"id":"danger","running":[{"action":"write.speed","arguments":[2000]}],
					 "transitions":[{"target":"hold","conditions":[{"left":{"observation":"console.stance"},"op":"eq","right":"hold"}]}]},
					{"id":"hold","running":[{"action":"write.speed","arguments":[0]}]}
				]})"));
			ship["control_layout"]["controllers"][0]["tree"] = "test.bridge-ai";
			source["maps"][0]["entities"].push_back(
			    {{"id", 2}, {"definition", "test.controllable"}, {"x_mm", 100000}, {"y_mm", 0}, {"heading_mdeg", 0}});
			const auto catalog = load(source);
			World world;
			world.load(catalog, "test.map");
			require(world.claim(102, 1, "observer"), "alert owner claims its Console");
			require(!world.claim(31, 1, "command") && world.claim(31, 1, "command", true),
			        "supplemental Command requires explicit host sidebar admission");
			require(!world.submit(31, {1, "alert.set", 1, {}}), "Command does not inherit alert authority");
			require(world.submit(102, {1, "alert.set", 1, {}}), "owner selects authored alert");
			world.tick();
			require(world.public_board(1).at("alert").at("id") == "danger" &&
			            world.public_board(2).at("alert").at("id") == "routine",
			        "alert state is ship-local");
			world.tick();
			require(world.observe(31)->gameplay.at("alert").at("severity") == 1, "all Consoles see accepted condition");
			require(world.submit(31, {1, "stance.set", 1, "drive"}), "Command may select fully AI Console stance");
			world.tick();
			world.tick();
			require(world.claim(101, 1, "drive"), "human takes over drive");
			require(!world.public_board(1).at("sidebar")[0].at("visible").get<bool>(),
			        "Command auto-hides when no eligible AI target remains");
			require(world.observe(101)->gameplay.at("stance").at("id") == "hold", "stance survives takeover");
			require(controller(world, 101, "propulsion").at("commands").at("speed").at("value") == 0,
			        "AI tree reads alert identity, severity and Console stance");
			require(!world.submit(31, {1, "stance.set", 0, "drive"}), "Command refuses human Console");
			require(world.submit(101, {1, "stance.set", 0, {}}), "own operator may select stance");
			world.tick();
			require(world.release(101), "drive returns to AI");
			require(world.public_board(1).at("sidebar")[0].at("visible").get<bool>(),
			        "Command becomes available when an AI target returns");
			OperatingCommand delayed_stance{1, "stance.set", 1, "drive"};
			const auto command_view = world.observe(31);
			require(!command_view->gameplay.contains("profile") &&
			            std::none_of(command_view->gameplay.at("controls").begin(),
			                         command_view->gameplay.at("controls").end(),
			                         [](const auto &control)
			                         {
				                         return control.at("action") == "preset";
			                         }),
			        "Command exposes stance controls without artificial complexity profiles");

			for (const auto &control : command_view->gameplay.at("controls"))
				if (control.at("action") == "stance.set" && control.at("system") == "drive")
					delayed_stance.generation = control.at("generation");
			require(world.submit(31, delayed_stance) && world.claim(101, 1, "drive"),
			        "queue stance before human takeover");
			world.tick();
			require(world.observe(101)->gameplay.at("stance").at("id") == "routine",
			        "execution rejects stale Command authority");
			const auto revision = world.public_board(1).at("alert").at("revision");
			AlertAudioTransition audio;
			auto sound = world.public_board(1).at("alert");
			require(!audio.observe(&world, 1, sound, true), "joining active condition is silent");
			sound["revision"] = revision.get<std::uint64_t>() + 1;
			require(audio.observe(&world, 1, sound, true) && !audio.observe(&world, 1, sound, true),
			        "live entry sounds once across repeated frames");
			require(!audio.observe(&world, 2, sound, true), "save restoration does not replay siren");
			require(!audio.observe(&world, 2, sound, false), "remote clients do not sound ship siren");
			require(condition_music(sound).empty(), "missing condition track uses mission music fallback");
			sound["music"] = "test:music.ogg";
			require(condition_music(sound) == "test:music.ogg", "audio reads authored condition track");
			require(world.submit(102, {1, "alert.set", 1, {}}), "reselect current condition");
			world.tick();
			require(world.public_board(1).at("alert").at("revision") == revision,
			        "same condition has no new siren transition");
			World restored;
			restored.restore(catalog, world.save());
			require(restored.public_board(1).at("alert") == world.public_board(1).at("alert") &&
			            restored.observe(101)->gameplay.at("stance") == world.observe(101)->gameplay.at("stance"),
			        "checkpoint preserves accepted alert and stance");
			const auto public_data = network::public_board_json(world.public_board(1));
			require(public_data.contains("alert") && !public_data.contains("stance") &&
			            !public_data.contains("controllers"),
			        "public alert projection preserves private Console boundaries");
			auto invalid = source;
			invalid["entities"][0]["bridge"]["initial"] = "missing";
			bool refused = false;
			try
			{
				(void)load(invalid);
			}
			catch (const std::exception &)
			{
				refused = true;
			}
			require(refused, "loader rejects unresolved default condition");
			auto session_source = source;
			session_source["entities"][0]["systems"]["steering"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}};
			session_source["maps"][0]["entities"] = Json::array();
			session_source["maps"][0]["ship_slots"] = Json::array({{{"id", "test.slot"},
			                                                        {"x_mm", 0},
			                                                        {"y_mm", 0},
			                                                        {"heading_mdeg", 0},
			                                                        {"allowed_ships", {"test.controllable"}}}});
			auto host = make_host_world();
			host->add_pack(archive(session_source));
			host->start_server({"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "observer"});
			GameExecution execution(host, {{"server", "test.slot"}});
			const auto host_ship = *host->authoritative_world().ship_for_slot("test.slot");
			const ConsoleAddress command_address{host_ship, "command"};
			require(!execution.can_claim("server", "remote", 31, command_address, false) &&
			            !execution.can_claim("other-server", "local-console", 31, command_address, false),
			        "sidebar lease is local-host-only and ship-scoped");
			require(execution.apply("server", {{"kind", "station-claim"},
			                                   {"owner", "local-console"},
			                                   {"route", 31},
			                                   {"ship", host_ship},
			                                   {"seat", "command"},
			                                   {"lease", std::string(64, 'a')}}),
			        "host sidebar claim uses committed lease execution");
			const auto sidebar = execution.observe("server", "local-console", 31);
			require(sidebar && sidebar->console == "command" && sidebar->gameplay.at("controls").size() > 1,
			        "leased Command receives interactive permitted controls");
			require(!execution.can_claim("server", "another", 31, command_address, true),
			        "sidebar cannot be silently replaced");
			require(execution.apply("server", {{"kind", "station-release"},
			                                   {"owner", "local-console"},
			                                   {"route", 31},
			                                   {"lease", std::string(64, 'a')},
			                                   {"forget", true}}),
			        "closing sidebar releases authority through committed execution");
		}
		{
			NavigationCamera camera;
			camera.fit(-10000, -20000, 90000, 20000, 1000, 500);
			require(camera.x == 40000 && camera.y == 0 && camera.scale >= 100,
			        "map fits ship and destination with padding");
			const auto world_x = camera.x + 100 * camera.scale, world_y = camera.y - 50 * camera.scale;
			camera.zoom(.5, 100, 50);
			require(camera.x + 100 * camera.scale == world_x && camera.y - 50 * camera.scale == world_y,
			        "zoom preserves cursor world position");
			const auto scale = camera.scale;
			camera.fit(0, 0, 1000000, 1000000, 1000, 500);
			require(camera.scale == scale, "manual view survives automatic fit updates");
			camera.automatic = true;
			camera.fit(0, 0, 1000000, 1000000, 1000, 500);
			require(camera.scale > scale, "reset refits current level extents");
		}
		{
			auto source = controller_fixture();
			auto &entity = source["entities"][0];
			entity["systems"]["radar"] = {{"kind", "radar"}, {"range_mm", 100000}};
			entity["systems"]["wide"] = {{"kind", "radar"}, {"range_mm", 200000}};
			entity["stations"][0]["consoles"][0]["feeds"].push_back("radar");
			entity["stations"][0]["consoles"][0]["feeds"].push_back("wide");
			entity["stations"][0]["consoles"][1]["definition"] = "test.nav";
			source["consoles"][0]["widgets"] = Json::array({{{"id", "scope"},
			                                                 {"kind", "radar"},
			                                                 {"label", "Radar"},
			                                                 {"binding", "radar.contacts"},
			                                                 {"system", "radar"},
			                                                 {"x", 0},
			                                                 {"y", 0},
			                                                 {"width", 1000},
			                                                 {"height", 1000}}});
			source["consoles"].push_back({{"id", "test.nav"},
			                              {"widgets", Json::array({{{"id", "map"},
			                                                        {"kind", "navigation"},
			                                                        {"label", "Navigation"},
			                                                        {"binding", "ship.navigation"},
			                                                        {"controller", "planner"},
			                                                        {"channel", "point"},
			                                                        {"x", 0},
			                                                        {"y", 0},
			                                                        {"width", 1000},
			                                                        {"height", 1000}}})}});
			entity["control_layout"]["controllers"].push_back(
			    {{"id", "planner"},
			     {"console", "observer"},
			     {"fallback", "hold"},
			     {"commands", Json::array({{{"id", "point"},
			                                {"action", "waypoint"},
			                                {"initial", 0},
			                                {"arguments", {0, 0}},
			                                {"consumers", Json::array()}}})}});
			for (auto &profile : entity["control_layout"]["profiles"])
				profile["consoles"]["observer"] = {{"human", {"planner"}}};
			for (int id = 2; id <= 3; ++id)
				source["maps"][0]["entities"].push_back({{"id", id},
				                                         {"definition", "test.controllable"},
				                                         {"x_mm", id == 2 ? 10000 : 150000},
				                                         {"y_mm", 0},
				                                         {"heading_mdeg", 0}});
			source["maps"][0]["regions"] = Json::array({{{"id", "hidden-cloud"},
			                                             {"shape", "circle"},
			                                             {"x_mm", -25000},
			                                             {"y_mm", 0},
			                                             {"radius_mm", 1000},
			                                             {"nebula", {{"density", .01}, {"sensor_factor", 1}}}}});
			const auto catalog = load(source);
			World world;
			world.load(catalog, "test.map");
			require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer") && world.claim(103, 2, "drive"),
			        "independent shared-display operators claim synthetic consoles");
			OperatingCommand radar{1, "display.toggle", 0, "scope"};
			radar.console = "drive";
			radar.arguments = {0, 0, 1};
			OperatingCommand nav{1, "display.toggle", 0, "map"};
			nav.console = "observer";
			nav.arguments = {50000, 0, 200000};
			auto invalid = radar;
			invalid.system = "missing";
			require(!world.submit(101, invalid) && !world.submit(102, radar),
			        "unavailable or foreign display source refuses");
			invalid = radar;
			invalid.arguments = {0, 0, 0};
			require(!world.submit(101, invalid), "shared camera rejects zero extent");
			require(world.submit(101, radar), "radar sharing admitted through ordinary command path");
			world.tick();
			const auto radar_view = world.public_board(1).at("display");
			require(radar_view.at("kind") == "radar" && radar_view.at("contacts").size() == 1 &&
			            radar_view.at("contacts")[0].at("id") == 2 && !world.public_board(2).contains("display"),
			        "shared radar exposes only its selected sensor contacts and stays on its ship");
			require(!radar_view.at("radar").at("reach_profile").empty() &&
			            radar_view.dump().find("hidden-cloud") == std::string::npos &&
			            !radar_view.at("radar").contains("nebula"),
			        "Shared Viewscreen lost adaptive boundary or leaked hidden region geometry");
			auto dirty = world.public_board(1);
			dirty["display"]["controls"] = {"secret"};
			dirty["display"]["own"]["private"] = "secret";
			const auto transported = network::public_board_json(dirty);
			require(transported.at("display") == radar_view && transported.dump().find("secret") == std::string::npos,
			        "remote shared-display codec reconstructs only visual fields");
			auto crowded = radar_view;
			crowded["contacts"] = Json::array();
			for (int i = 0; i < 4096; ++i)
			{
				auto contact = radar_view.at("contacts")[0];
				contact["id"] = i + 10;
				contact["definition"] = std::string(256, 'x');
				crowded["contacts"].push_back(std::move(contact));
			}
			const auto bounded = display::sanitise(crowded);
			require(bounded.at("contacts").size() < 4096 &&
			            bounded.at("contacts").size() + bounded.at("omitted_contacts").get<std::size_t>() == 4096 &&
			            display::sanitise(bounded) == bounded,
			        "crowded valid radar projection stays bounded and reports omitted contacts without failure");
			auto stale = radar;
			stale.action = "display.update";
			stale.generation = radar_view.at("revision");
			stale.arguments = {9, 9, 100};
			require(world.submit(102, nav) && world.submit(101, stale),
			        "replacement and earlier publisher update can be queued in one tick");
			world.tick();
			require(world.public_board(1).at("display").at("kind") == "navigation" &&
			            world.public_board(1).at("display").at("camera") == Json(nav.arguments) &&
			            !world.submit(101, stale),
			        "last display command wins and stale live updates cannot reclaim the screen");
			auto moved = nav;
			moved.action = "display.update";
			moved.generation = world.public_board(1).at("display").at("revision");
			moved.arguments = {80000, 20000, 100000};
			require(world.submit(102, moved), "current source pans and zooms live");
			world.tick();
			require(world.public_board(1).at("display").at("camera") == Json(moved.arguments),
			        "shared navigation follows current browsing");
			World resumed;
			resumed.restore(catalog, world.save());
			require(resumed.public_board(1) == world.public_board(1),
			        "replica checkpoint preserves current shared view");
			resumed.clear_claims();
			require(!resumed.public_board(1).contains("display"), "user recovery clears transient sharing with claims");
			require(world.submit(102, nav), "current source toggles back");
			world.tick();
			require(!world.public_board(1).contains("display"),
			        "off restores normal view without resurrecting old radar");
			require(world.submit(101, radar) && world.submit(102, nav) && world.submit(101, radar),
			        "competing explicit source changes queue in arrival order");
			world.tick();
			require(world.public_board(1).at("display").at("kind") == "radar",
			        "last explicit source replaces every earlier source in the tick");
			require(world.release(101) && !world.public_board(1).contains("display"), "publisher release ends sharing");
			auto session_source = source;
			session_source["entities"][0]["systems"]["steering"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}};
			session_source["entities"][0]["stations"][0]["consoles"][0]["feeds"].push_back("steering");
			session_source["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
			    {{"id", "heading"}, {"action", "heading_mdeg"}, {"consumers", {"steering"}}});
			session_source["maps"][0]["ship_slots"] = Json::array({{{"id", "test.slot"},
			                                                        {"x_mm", 300000},
			                                                        {"y_mm", 0},
			                                                        {"heading_mdeg", 0},
			                                                        {"allowed_ships", {"test.controllable"}}}});
			auto session = make_local_session();
			session->add_pack(archive(session_source));
			session->start({"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "drive"});
			ConsoleModel model;
			model.refresh(session);
			require(model.on_screen("scope", {0, 0, 1}), "ConsoleModel publishes verified radar widget");
			session->tick();
			require(model.is_on_screen("scope"), "source reads shared active state");
			Application app;
			app.session = session;
			app.screen_controls = {{"console/1/on-screen/scope", "On screen"}};
			app.screen_toggles = {{"console/1/on-screen/scope", model.is_on_screen("scope")}};
			const auto semantics = navigation_controls(app);
			require(std::any_of(semantics.begin(), semantics.end(),
			                    [](const auto &c)
			                    {
				                    return c.id == "console/1/on-screen/scope" && c.toggle && c.pressed;
			                    }),
			        "semantic controls expose the source's pressed state");
			auto recovered = make_local_session();
			recovered->restore(session->save());
			recovered->resume_local();
			require(!recovered->public_board().contains("display"),
			        "portable session resume starts on normal viewscreen");
		}

		for (const bool visible : {false, true})
		{
			auto source = controller_fixture();
			auto &entity = source["entities"][0];
			entity["systems"]["radar"] = {{"kind", "radar"}, {"range_mm", visible ? 100000 : 1000}};
			entity["stations"][0]["consoles"][0]["feeds"].push_back("radar");
			entity["control_layout"]["controllers"][0]["tree"] = "test.clearance";
			entity["control_layout"]["controllers"][0].erase("fallback");
			entity["control_layout"]["controllers"].push_back(
			    {{"id", "sensor"},
			     {"console", "drive"},
			     {"fallback", "hold"},
			     {"commands",
			      Json::array(
			          {{{"id", "enabled"}, {"action", "radar_enabled"}, {"initial", 1}, {"consumers", {"radar"}}}})}});
			for (auto &profile : entity["control_layout"]["profiles"])
				profile["consoles"]["drive"]["ai"].push_back("sensor");
			source["entities"].push_back(
			    {{"id", "test.rock"},
			     {"render", entity["render"]},
			     {"systems", Json::object()},
			     {"collision", {{"shape", "circle"}, {"radius_mm", 1000}, {"mass_grams", 1000000}}}});
			source["maps"][0]["entities"].push_back(
			    {{"id", 2}, {"definition", "test.rock"}, {"x_mm", 10000}, {"y_mm", 0}, {"heading_mdeg", 0}});
			source["trees"] = Json::parse(
			    R"json([{"id":"test.clearance","context":"controller","initial":"clear","command_bindings":[{"channel":"speed"}],"states":[{"id":"clear","running":[{"action":"write.speed","arguments":[1000]}],"transitions":[{"target":"blocked","conditions":[{"left":{"observation":"navigation.blocked"},"op":"eq","right":true}]}]},{"id":"blocked","running":[{"action":"write.speed","arguments":[0]}],"transitions":[{"target":"clear","conditions":[{"left":{"observation":"navigation.blocked"},"op":"eq","right":false}]}]}]}])json");
			World world;
			world.load(load(source), "test.map");
			for (int i = 0; i < 5; ++i)
				world.tick();
			world.claim(101, 1, "drive");
			require(requested(world, "port") == (visible ? 0 : 1000),
			        "clearance must use permitted contacts rather than hidden obstacles");
		}
		{
			auto source = controller_fixture();
			auto &entity = source["entities"][0];
			auto observer = entity["stations"][0]["consoles"][1];
			entity["stations"][0]["consoles"].erase(1);
			entity["stations"].push_back({{"id", "support"}, {"consoles", Json::array({observer})}});
			source["consoles"].push_back({{"id", "test.seeking"}, {"human_seeking", true}, {"widgets", Json::array()}});
			for (const auto *name : {"navigation", "reports"})
				entity["stations"][0]["consoles"].push_back({{"id", name},
				                                             {"definition", "test.seeking"},
				                                             {"systems", Json::array()},
				                                             {"human_preferences", {"support", "operations"}}});
			entity["control_layout"]["controllers"].push_back(
			    {{"id", "planner"},
			     {"console", "navigation"},
			     {"fallback", "hold"},
			     {"commands", Json::array({{{"id", "point"},
			                                {"action", "waypoint"},
			                                {"initial", 0},
			                                {"arguments", {0, 0}},
			                                {"consumers", Json::array()}}})}});
			for (auto &profile : entity["control_layout"]["profiles"])
				profile["consoles"]["navigation"] = {{"human", {"planner"}}};
			const auto catalog = load(source);
			World world;
			world.load(catalog, "test.map");
			require(world.claim(101, 1, "drive"), "fallback operator can claim primary console");
			auto first = world.observe(101);
			require(first->additional_consoles.size() == 2, "any human-seeking console follows available human");
			const auto planner = first->additional_consoles[0].gameplay.at("controllers")[0];
			OperatingCommand command{1, "waypoint", 1, {}, "planner", "point", planner.at("generation")};
			command.console = "navigation";
			command.arguments = {30000, 0};
			require(world.submit(101, command), "additional Console admits its owner's waypoint");
			world.tick();
			require(world.claim(102, 1, "observer"), "preferred support operator joins");
			require(world.observe(101)->additional_consoles.empty() &&
			            world.observe(102)->additional_consoles.size() == 2,
			        "preference transfers both generic Consoles");
			require(!world.submit(101, command) && !world.submit(102, command),
			        "old operator and old generation cannot write after transfer");
			command.generation =
			    world.observe(102)->additional_consoles[0].gameplay.at("controllers")[0].at("generation");
			require(world.submit(102, command), "new operator generation can write");
			world.tick();
			World restored;
			restored.restore(catalog, world.save());
			require(restored.observe(102)->additional_consoles.size() == 2,
			        "recovery derives same supplemental assignment");
			require(world.release(102) && world.observe(101)->additional_consoles.size() == 2,
			        "departure restores fallback assignment");
			bool shared = false;
			const auto final_view = world.observe(101);
			for (const auto &record : final_view->gameplay.at("shared"))
				shared |= record.at("schema") == "phoenix.waypoint";
			require(shared, "waypoint is published shipwide");
		}
		{
			auto source = controller_fixture();
			source["entities"][0]["control_layout"]["controllers"][0]["tree"] = "test.mutable";
			source["entities"][0]["control_layout"]["controllers"][0].erase("fallback");
			source["trees"] = Json::parse(R"json([{"id":"test.mutable","context":"controller","initial":"run",
 "command_bindings":[{"channel":"speed"}],"states":[{"id":"run","entry":[{"action":"write.speed","arguments":[1000]}]}]}])json");
			auto catalog = load(source);
			catalog.trees.at("test.mutable")["states"][0]["entry"][0]["arguments"][0] = 5000;
			World changed;
			changed.load(catalog, "test.map");
			changed.tick();
			require(changed.claim(101, 1, "drive") && requested(changed, "port") == 5000 &&
			            requested(changed, "starboard") == 5000,
			        "World must execute the freshly validated authored Controller tree rather than stale Catalog "
			        "program cache");
			const auto saved = changed.save();
			catalog.trees.at("test.mutable")["states"][0]["entry"][0]["action"] = "write.unknown";
			rejects(
			    [&]
			    {
				    changed.load(catalog, "test.map");
			    },
			    "Invalid mutated authored tree must be refused despite valid old cache");
			require(changed.save() == saved, "Failed mutable-Catalog load must preserve live World transactionally");
		}

		for (const bool split_vector : {false, true})
		{
			auto vector_source = controller_fixture(split_vector);
			auto &vector_entity = vector_source["entities"][0];
			vector_entity["motion_model"] = "vector";
			vector_entity["systems"]["turn"] = {{"kind", "steering"}, {"turn_mdeg_s", 90001}};
			vector_entity["systems"]["port"]["acceleration_mm_s2"] = 2001;
			vector_entity["systems"]["starboard"]["acceleration_mm_s2"] = 1003;
			vector_entity["stations"][0]["consoles"][0]["feeds"].push_back("turn");
			auto &vector_controls = vector_entity["control_layout"]["controllers"];
			vector_controls[0]["commands"].push_back(
			    {{"id", "heading"}, {"action", "heading_mdeg"}, {"consumers", {"turn"}}});
			vector_controls[0]["commands"].push_back({{"id", "enabled"},
			                                          {"action", "system_enabled"},
			                                          {"initial", 1},
			                                          {"consumers", {"port", "starboard"}}});
			vector_source["maps"][0]["entities"][0]["heading_mdeg"] = 33000;
			auto legacy_source = vector_source;
			auto &legacy_entity = legacy_source["entities"][0];
			legacy_entity.erase("motion_model");
			legacy_entity.erase("control_layout");
			legacy_entity["stations"][0]["consoles"][0]["systems"] = {"port", "starboard", "turn"};
			const auto vector_catalog = load(vector_source), legacy_catalog = load(legacy_source);
			World vector_world, legacy_world;
			vector_world.load(vector_catalog, "test.map");
			legacy_world.load(legacy_catalog, "test.map");
			require(vector_world.claim(101, 1, "drive") && legacy_world.claim(101, 1, "drive"),
			        "vector and legacy comparison claims");
			const std::string first = split_vector ? "port_drive" : "propulsion";
			require(controller(vector_world, 101, first).at("commands").at("heading").at("value") == 33000,
			        "omitted heading initial preserves spawn pose");
			for (int tick = 0; tick < 45; ++tick)
			{
				auto submit_vector = [&](const std::string &owner, const std::string &channel, std::int64_t value)
				{
					OperatingCommand command{1, {}, value, {}};
					command.controller = owner;
					command.channel = channel;
					command.generation = controller(vector_world, 101, owner).at("generation");
					require(vector_world.submit(101, command), "vector Controller command admitted");
				};
				if (tick == 0 || tick == 22)
				{
					submit_vector(first, "speed", 7000);
					if (split_vector)
						submit_vector("starboard_drive", "speed", 3000);
					submit_vector(first, "heading", tick == 0 ? 90000 : 250000);
					submit_vector(first, "enabled", 1);
					require(legacy_world.submit(101, {1, "speed_mm_s", 7000, "port"}) &&
					            legacy_world.submit(101, {1, "speed_mm_s", split_vector ? 3000 : 7000, "starboard"}) &&
					            legacy_world.submit(101, {1, "heading_mdeg", tick == 0 ? 90000 : 250000, "turn"}) &&
					            legacy_world.submit(101, {1, "engine_enabled", 1, "port"}) &&
					            legacy_world.submit(101, {1, "engine_enabled", 1, "starboard"}),
					        "legacy sequence admitted");
				}
				if (tick == 13)
				{
					submit_vector(first, "enabled", 0);
					require(legacy_world.submit(101, {1, "engine_enabled", 0, "port"}) &&
					            legacy_world.submit(101, {1, "engine_enabled", 0, "starboard"}),
					        "legacy coast");
				}
				vector_world.tick();
				legacy_world.tick();
				require(vector_world.save().at("bodies")[0].at("body") ==
				            legacy_world.save().at("bodies")[0].at("body"),
				        "vector Controller preserves legacy trajectory and integration remainders");
			}
			World vector_resumed;
			vector_resumed.restore(vector_catalog, vector_world.save());
			vector_world.tick();
			vector_resumed.tick();
			require(vector_world.save() == vector_resumed.save(), "vector controls restore exactly");

			auto timed_vector = vector_source;
			auto &timed_owner = timed_vector["entities"][0]["control_layout"]["controllers"][0];
			timed_owner.erase("fallback");
			timed_owner["tree"] = "test.vector.timed";
			timed_vector["trees"] = Json::parse(R"json([
  {
    "id": "test.vector.timed",
    "context": "controller",
    "initial": "run",
    "command_bindings": [
      {
        "channel": "speed",
        "type": "integer",
        "semantics": "setting"
      }
    ],
    "states": [
      {
        "id": "run",
        "running": [
          {
            "action": "write.speed",
            "arguments": [
              5000
            ],
            "once": true
          }
        ],
        "transitions": [
          {
            "target": "run",
            "conditions": [
              {
                "left": {
                  "elapsed": true
                },
                "op": "ge",
                "right": 10
              }
            ]
          }
        ]
      }
    ]
  }
])json");
			const auto timed_catalog = load(timed_vector);
			World timed_ai, timed_human;
			timed_ai.load(timed_catalog, "test.map");
			timed_human.load(timed_catalog, "test.map");
			require(timed_human.claim(101, 1, "drive"), "vector timed human claim");
			require(timed_human.submit(101, speed(timed_human, 101, first, 5000)), "equivalent human vector command");
			for (int tick = 0; tick < 12; ++tick)
			{
				timed_ai.tick();
				timed_human.tick();
				require(timed_ai.save().at("bodies")[0].at("body") == timed_human.save().at("bodies")[0].at("body"),
				        "vector timed AI and human share physical consumer");
			}
			auto explicit_heading = vector_source;
			explicit_heading["entities"][0]["control_layout"]["controllers"][0]["commands"][1]["initial"] = 12000;
			World heading_override;
			heading_override.load(load(explicit_heading), "test.map");
			require(heading_override.claim(101, 1, "drive"), "explicit heading claim");
			require(controller(heading_override, 101, first).at("commands").at("heading").at("value") == 12000,
			        "explicit heading initial overrides pose");
			auto bounded_heading = vector_source;
			bounded_heading["entities"][0]["control_layout"]["controllers"][0]["commands"][1]["maximum"] = 20000;
			const auto before_bad_pose = vector_world.save();
			rejects(
			    [&]
			    {
				    vector_world.load(load(bounded_heading), "test.map");
			    },
			    "out-of-bounds spawn heading refused");
			require(vector_world.save() == before_bad_pose, "invalid spawn heading load is transactional");
			for (const auto *bad_action : {"destination", "longitudinal_thrust"})
			{
				auto bad = vector_source;
				auto &command = bad["entities"][0]["control_layout"]["controllers"][0]["commands"][0];
				command["action"] = bad_action;
				if (std::string(bad_action) == "destination")
					command["arguments"] = {1000, 2000};
				rejects(
				    [&]
				    {
					    load(bad);
				    },
				    "vector rejects inertial actions");
			}
			auto paid_vector = vector_source;
			paid_vector["entities"][0]["systems"]["port"]["power_rate"] = 10;
			rejects(
			    [&]
			    {
				    load(paid_vector);
			    },
			    "vector rejects resource cost bindings");
			auto invalid_model = vector_source;
			invalid_model["entities"][0]["motion_model"] = "unknown";
			rejects(
			    [&]
			    {
				    load(invalid_model);
			    },
			    "unknown motion model rejected");
		}
		const auto content = load(controller_fixture());
		auto preference_source = controller_fixture();
		auto &preference_layout = preference_source["entities"][0]["control_layout"];
		preference_layout["controllers"][0].erase("fallback");
		preference_layout["controllers"][0]["tree"] = "test.preference";
		preference_layout["controllers"][0]["commands"].push_back({{"id", "enabled"},
		                                                           {"action", "engine_enabled"},
		                                                           {"type", "boolean"},
		                                                           {"initial", 1},
		                                                           {"consumers", {"port", "starboard"}}});
		preference_layout["controllers"].push_back(
		    Json::parse(R"json({"id":"preference","console":"drive","fallback":"hold",
		 "commands":[{"id":"cruise","action":"intent_integer","minimum":0,"maximum":6000,"consumers":[]},
		 {"id":"enabled","action":"intent_boolean","type":"boolean","initial":1,"consumers":[]}]})json"));
		preference_layout["profiles"][0]["consoles"]["drive"] = {{"human", {"preference"}}, {"ai", {"propulsion"}}};
		preference_layout["profiles"][1]["consoles"]["drive"] = {{"ai", {"preference", "propulsion"}}};
		const auto preference_trees = R"json([{"id":"test.preference","context":"controller","initial":"run",
		 "command_bindings":[{"channel":"speed"},{"channel":"enabled","type":"boolean"}],
		 "command_reads":[{"name":"cruise","controller":"preference","channel":"cruise"},
		  {"name":"enabled","controller":"preference","channel":"enabled","type":"boolean"}],
		 "states":[{"id":"run","running":[{"action":"write.speed","arguments":[{"observation":"cruise"}]},
		 {"action":"write.enabled","arguments":[{"observation":"enabled"}]}]}]}])json";
		preference_source["trees"] = Json::parse(preference_trees);
		const auto preference_catalog = load(preference_source);
		World preferences;
		preferences.load(preference_catalog, "test.map");
		require(preferences.claim(101, 1, "drive"),
		        "human owns coarse preference while actuator Controller remains AI");
		OperatingCommand preference_command{1, {}, 5000, {}};
		preference_command.controller = "preference";
		preference_command.channel = "cruise";
		preference_command.generation = controller(preferences, 101, "preference").at("generation");
		auto bad_preference = preference_command;
		bad_preference.value = 6001;
		require(!preferences.submit(101, bad_preference),
		        "intent numeric range is authoritative without physical consumers");
		bad_preference.channel = "enabled";
		bad_preference.value = 2;
		require(!preferences.submit(101, bad_preference), "boolean preference rejects nonboolean value");
		const auto preference_view = preferences.observe(101);
		require(std::any_of(preference_view->gameplay.at("controls").begin(),
		                    preference_view->gameplay.at("controls").end(),
		                    [](const auto &control)
		                    {
			                    return control.value("action", "") == "intent_integer";
		                    }),
		        "generated human controls expose coarse preference settings");
		require(preferences.submit(101, preference_command), "human coarse preference admitted");
		require(requested(preferences, "port") == 0 && requested(preferences, "starboard") == 0,
		        "queued intent does not immediately mutate mechanisms");
		preferences.tick();
		require(requested(preferences, "port") == 5000 && requested(preferences, "starboard") == 5000,
		        "peer AI translates the tick-boundary committed preference snapshot into shared physical command");
		auto boolean_preference = preference_command;
		boolean_preference.channel = "enabled";
		boolean_preference.value = 0;
		require(preferences.submit(101, boolean_preference), "human boolean preference admitted");
		preferences.tick();
		preferences.tick();
		const auto disabled_preference = preferences.observe(101);
		for (const auto &system : disabled_preference->systems)
			if (system.id == "port" || system.id == "starboard")
				require(!system.enabled, "boolean preference reaches AI actuator decision");
		require(preferences.submit(101, {1, "preset", 1, {}}), "preference owner yields through complexity");
		preferences.tick();
		require(!preferences.submit(101, preference_command) &&
		            controller(preferences, 101, "preference").at("commands").at("cruise").at("value") == 5000,
		        "handover invalidates human intent generation while preserving preference");
		World preference_resumed;
		preference_resumed.restore(preference_catalog, preferences.save());
		preferences.tick();
		preference_resumed.tick();
		require(preferences.save() == preference_resumed.save(), "coarse intent and AI continuation restore exactly");
		auto physical_preference = preference_source;
		physical_preference["entities"][0]["control_layout"]["controllers"][1]["commands"][0]["consumers"] = {"port"};
		rejects(
		    [&]
		    {
			    load(physical_preference);
		    },
		    "coarse intent cannot bind a physical mechanism");
		for (const bool sensor_feed : {false, true})
		{
			auto contact_source = controller_fixture();
			auto &contact_entity = contact_source["entities"][0];
			contact_entity["systems"]["radar"] = {{"kind", "radar"}, {"range_mm", 100000}};
			if (sensor_feed)
				contact_entity["stations"][0]["consoles"][0]["feeds"].push_back("radar");
			auto &contact_layout = contact_entity["control_layout"];
			contact_layout["controllers"][0].erase("fallback");
			contact_layout["controllers"][0]["tree"] = "test.contacts";
			contact_layout["controllers"][0]["commands"].push_back(
			    {{"id", "target"}, {"action", "selected_contact"}, {"consumers", {"port"}}});
			contact_layout["controllers"].push_back({{"id", "sensor"},
			                                         {"console", "drive"},
			                                         {"fallback", "hold"},
			                                         {"commands", Json::array({{{"id", "enabled"},
			                                                                    {"action", "radar_enabled"},
			                                                                    {"type", "boolean"},
			                                                                    {"initial", 1},
			                                                                    {"consumers", {"radar"}}}})}});
			contact_layout["profiles"][0]["consoles"]["drive"] = {{"human", {"sensor"}}, {"ai", {"propulsion"}}};
			contact_layout["profiles"][1]["consoles"]["drive"] = {{"ai", {"sensor", "propulsion"}}};
			contact_source["trees"] = Json::parse(R"json([{"id":"test.contacts","context":"controller","initial":"run",
			 "command_bindings":[{"channel":"speed"},{"channel":"target"}],
			 "contact_bindings":[{"name":"target","controller":"propulsion","channel":"target"}],
			 "states":[{"id":"run","running":[{"action":"write.target","arguments":[2]},
			 {"action":"write.speed","arguments":[{"observation":"target.x"}]}]}]}])json");
			contact_source["maps"][0]["entities"].push_back(
			    {{"id", 2}, {"definition", "test.controllable"}, {"x_mm", 5000}, {"y_mm", 0}, {"heading_mdeg", 0}});
			const auto contact_catalog = load(contact_source);
			World contacts;
			contacts.load(contact_catalog, "test.map");
			require(contacts.claim(101, 1, "drive"), "contact reader Console claim");
			contacts.tick();
			require(controller(contacts, 101, "propulsion").at("commands").at("speed").at("value") == 0,
			        "new selector cannot expose a contact to same-tick decision");
			contacts.tick();
			require(controller(contacts, 101, "propulsion").at("commands").at("speed").at("value") ==
			            (sensor_feed ? 5000 : 0),
			        "accepted selector only reads its permitted frozen Console contact feed");
			if (sensor_feed)
			{
				OperatingCommand disable{1, {}, 0, {}};
				disable.controller = "sensor";
				disable.channel = "enabled";
				disable.generation = controller(contacts, 101, "sensor").at("generation");
				require(contacts.submit(101, disable), "human sensor operator disables feed");
				contacts.tick();
				const auto stopped_value =
				    controller(contacts, 101, "propulsion").at("commands").at("speed").at("value");
				contacts.tick();
				require(contacts.observe(101)->contacts.empty() &&
				            controller(contacts, 101, "propulsion").at("commands").at("speed").at("value") ==
				                stopped_value,
				        "disabled feed cannot provide fresh contact values");
				World contacts_resumed;
				contacts_resumed.restore(contact_catalog, contacts.save());
				contacts.tick();
				contacts_resumed.tick();
				require(contacts.save() == contacts_resumed.save(), "contact-driven continuation restores exactly");
			}
			auto foreign_contact = contact_source;
			foreign_contact["entities"][0]["control_layout"]["controllers"][1]["console"] = "observer";
			foreign_contact["entities"][0]["control_layout"]["profiles"][0]["consoles"] = {
			    {"drive", {{"ai", {"propulsion"}}}}, {"observer", {{"human", {"sensor"}}}}};
			foreign_contact["entities"][0]["control_layout"]["profiles"][1]["consoles"] = {
			    {"drive", {{"ai", {"propulsion"}}}}, {"observer", {{"ai", {"sensor"}}}}};
			foreign_contact["trees"][0]["contact_bindings"][0]["controller"] = "sensor";
			foreign_contact["trees"][0]["contact_bindings"][0]["channel"] = "enabled";
			rejects(
			    [&]
			    {
				    load(foreign_contact);
			    },
			    "foreign Console contact selector refused");
		}
		for (const std::string navigation : {"destination", "follow_contact"})
		{
			auto navigation_source = controller_fixture();
			auto &navigation_entity = navigation_source["entities"][0];
			navigation_entity["systems"]["turn"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}};
			navigation_entity["systems"]["radar"] = {{"kind", "radar"}, {"range_mm", 100000}};
			navigation_entity["stations"][0]["consoles"][0]["feeds"].push_back("radar");
			const Json tuple =
			    navigation == "destination" ? Json::array({50000, 10000}) : Json::array({2, 10000, 4000});
			auto &navigation_controller = navigation_entity["control_layout"]["controllers"][0];
			navigation_controller["commands"] = Json::array({{{"id", "navigate"},
			                                                  {"action", navigation},
			                                                  {"type", "boolean"},
			                                                  {"initial", 0},
			                                                  {"arguments", tuple},
			                                                  {"consumers", {"port", "starboard", "turn"}}}});

			// Startup defaults must be consumed before the first human or AI navigation decision.
			navigation_controller["commands"].push_back(
			    {{"id", "idle_mode"},
			     {"action", navigation == "destination" ? "follow_contact" : "destination"},
			     {"initial", 0},
			     {"arguments", navigation == "destination" ? Json::array({2, 10000, 4000}) : Json::array({0, 0})},
			     {"consumers", {"port", "starboard", "turn"}}});
			navigation_controller["commands"].push_back(
			    {{"id", "idle_heading"}, {"action", "heading_mdeg"}, {"initial", 0}, {"consumers", {"turn"}}});

			navigation_controller["commands"].push_back(
			    {{"id", "release_thrust"}, {"action", "longitudinal_thrust"}, {"consumers", {"port", "starboard"}}});
			navigation_controller["commands"].push_back(
			    {{"id", "release_turn"}, {"action", "manual_steering"}, {"consumers", {"turn"}}});
			navigation_controller.erase("fallback");
			navigation_controller["tree"] = "test.navigation";
			Json tree_args = Json::array({true});
			for (const auto &argument : tuple)
				tree_args.push_back(argument);
			navigation_source["trees"] =
			    Json::parse(R"json([{"id":"test.navigation","context":"controller","initial":"run",
			 "command_bindings":[{"channel":"navigate","type":"boolean"}],
			 "states":[{"id":"run","running":[{"action":"write.navigate","arguments":[]}]}]}])json");
			navigation_source["trees"][0]["command_bindings"][0]["action"] = navigation;
			navigation_source["trees"][0]["states"][0]["running"][0]["arguments"] = tree_args;
			navigation_source["maps"][0]["entities"].push_back({{"id", 2},
			                                                    {"definition", "test.controllable"},
			                                                    {"x_mm", 50000},
			                                                    {"y_mm", 10000},
			                                                    {"heading_mdeg", 0}});
			const auto navigation_catalog = load(navigation_source);
			auto mismatched_navigation = navigation_source;
			mismatched_navigation["trees"][0]["command_bindings"][0]["action"] =
			    navigation == "destination" ? "follow_contact" : "destination";
			rejects(
			    [&]
			    {
				    load(mismatched_navigation);
			    },
			    "tree navigation tuple contract must match owned channel");
			World navigation_human, navigation_ai;
			navigation_human.load(navigation_catalog, "test.map");
			navigation_ai.load(navigation_catalog, "test.map");
			require(navigation_human.claim(101, 1, "drive"), "compound human Console claim");

			for (const auto *channel : {"release_thrust", "release_turn"})
				require(controller(navigation_human, 101, "propulsion")
				            .at("commands")
				            .at(channel)
				            .at("accepted")
				            .get<bool>(),
				        "neutral held startup releases are accepted in assisted mode");
			OperatingCommand navigation_command{1, {}, 1, {}};
			navigation_command.controller = "propulsion";
			navigation_command.channel = "navigate";
			navigation_command.generation = controller(navigation_human, 101, "propulsion").at("generation");
			navigation_command.arguments = tuple.get<std::vector<std::int64_t>>();
			auto incomplete = navigation_command;
			incomplete.arguments.pop_back();
			require(!navigation_human.submit(101, incomplete), "partial compound command refuses atomically");
			incomplete = navigation_command;
			incomplete.controller = "foreign";
			require(!navigation_human.submit(101, incomplete), "foreign compound Controller refuses");
			const auto encoded_navigation = encode_operating_command(navigation_command);
			require(decode_operating_command(encoded_navigation).arguments == navigation_command.arguments,
			        "command codec preserves complete navigation tuple");
			require(navigation_human.submit(101, navigation_command), "compound human intent admitted");
			for (int tick = 0; tick < 15; ++tick)
			{
				navigation_human.tick();
				navigation_ai.tick();

				if (tick == 0)
				{
					const auto first_state = navigation_ai.save();
					for (const auto &system : first_state.at("bodies")[0].at("gameplay"))
						if (system.at("id") == "port" || system.at("id") == "turn")
							require(system.at("flight")
							            .at(navigation == "destination" ? "navigating" : "following")
							            .get<bool>(),
							        "inactive sibling defaults cannot cancel the first AI navigation decision");
				}
				const auto human_body = navigation_human.inspect().front(), ai_body = navigation_ai.inspect().front();
				require(human_body.x == ai_body.x && human_body.y == ai_body.y && human_body.heading == ai_body.heading,
				        "human and AI compound navigation yield identical physical trajectory");
			}
			require(navigation_human.inspect().front().x != 0 || navigation_human.inspect().front().y != 0,
			        "compound parity journey must execute physical motion");

			require(navigation_ai.claim(202, 1, "drive"), "active AI navigation hands over to a human");
			const auto handed_navigation = navigation_ai.save();
			for (const auto &system : handed_navigation.at("bodies")[0].at("gameplay"))
				if (system.at("id") == "port" || system.at("id") == "turn")
					require(
					    system.at("flight").at(navigation == "destination" ? "navigating" : "following").get<bool>(),
					    "handover neutral releases preserve ongoing navigation guidance");
			const auto navigation_save = navigation_human.save();
			World navigation_resumed;
			navigation_resumed.restore(navigation_catalog, navigation_save);
			navigation_human.tick();
			navigation_resumed.tick();
			require(navigation_human.save() == navigation_resumed.save(), "compound continuation restores exactly");
			auto bad_navigation_save = navigation_save;
			bad_navigation_save["bodies"][0]["console_controllers"]["controllers"][0]["commands"][0]["arguments"] =
			    Json::array();
			const auto before_bad_navigation = navigation_resumed.save();
			rejects(
			    [&]
			    {
				    navigation_resumed.restore(navigation_catalog, bad_navigation_save);
			    },
			    "malformed saved compound tuple refuses");
			require(navigation_resumed.save() == before_bad_navigation, "compound restore refusal is transactional");
			require(navigation_human.submit(101, {1, "preset", 1, {}}), "compound Console yields to AI");
			navigation_human.tick();
			require(!navigation_human.submit(101, navigation_command),
			        "handover invalidates complete old navigation tuple");
			require(navigation_human.submit(101, {1, "preset", 0, {}}), "compound Console returns to human");
			navigation_human.tick();
			require(!navigation_human.submit(101, navigation_command), "return does not revive old tuple generation");
			navigation_command.generation = controller(navigation_human, 101, "propulsion").at("generation");
			require(navigation_human.submit(101, navigation_command), "fresh generation admits complete tuple");
			if (navigation == "destination")
			{
				auto snapshot_source = navigation_source;
				auto &commands = snapshot_source["entities"][0]["control_layout"]["controllers"][0]["commands"];
				commands[0]["arguments"] = {1000, 10000};
				commands.push_back({{"id", "cruise"}, {"action", "speed_mm_s"}, {"consumers", {"port", "starboard"}}});
				auto &tree = snapshot_source["trees"][0];
				tree["command_bindings"].push_back({{"channel", "cruise"}});
				tree["states"][0]["running"] = Json::parse(R"json([
				 {"action":"write.navigate","arguments":[true,2000,10000]},
				 {"action":"write.cruise","arguments":[{"observation":"command.navigate.x"}]}])json");

				for (const bool reverse_order : {false, true})
				{
					auto route_source = snapshot_source;
					auto &route_tree = route_source["trees"][0];
					route_tree["initial"] = "first";
					route_tree["states"] = Json::parse(R"json([
                      {"id":"first","running":[{"action":"write.navigate","arguments":[true,20000,10000]},
                        {"action":"write.cruise","arguments":[2000]}],
                       "transitions":[{"target":"second","conditions":[{"left":{"elapsed":true},"op":"ge","right":2}]}]},
                      {"id":"second","running":[{"action":"write.navigate","arguments":[true,30000,20000]},
                        {"action":"write.cruise","arguments":[2000]}]}])json");
					if (reverse_order)
					{
						auto &ordered = route_source["entities"][0]["control_layout"]["controllers"][0]["commands"];
						std::reverse(ordered.begin(), ordered.end());
					}
					const auto route_catalog = load(route_source);
					World route;
					route.load(route_catalog, "test.map");
					require(route.prepare_console_profile({1, "drive"}, "automatic") && route.claim(101, 1, "drive"),
					        "route AI visible");
					for (int tick = 0; tick < 6; ++tick)
						route.tick();
					auto assert_cruise = [&]
					{
						const auto observation = route.observe(101);
						for (const auto &system : observation->systems)
							if (system.id == "port" || system.id == "starboard")
								require(
								    system.requested == 2000 && system.telemetry.at("navigating") == 1,
								    "new destination composes unchanged shared cruise independently of channel order");
					};
					assert_cruise();
					const auto cruise_revision =
					    controller(route, 101, "propulsion").at("commands").at("cruise").at("revision");
					require(controller(route, 101, "propulsion").at("commands").at("navigate").at("arguments") ==
					            Json::array({30000, 20000}),
					        "second route destination executed");
					require(route.submit(101, {1, "preset", 0, {}}), "route handed to human");
					route.tick();
					OperatingCommand destination{1, {}, 1, {}};
					destination.controller = "propulsion";
					destination.channel = "navigate";
					destination.generation = controller(route, 101, "propulsion").at("generation");
					destination.arguments = {40000, 30000};
					require(route.submit(101, destination), "human changes destination without changing cruise");
					route.tick();
					assert_cruise();
					require(controller(route, 101, "propulsion").at("commands").at("cruise").at("revision") ==
					            cruise_revision,
					        "composition does not forge a cruise revision");
					World resumed;
					resumed.restore(route_catalog, route.save());
					route.tick();
					resumed.tick();
					require(route.save() == resumed.save(), "composed route resumes exactly");
				}
				World snapshot_world;
				snapshot_world.load(load(snapshot_source), "test.map");
				require(snapshot_world.prepare_console_profile({1, "drive"}, "automatic") &&
				            snapshot_world.claim(101, 1, "drive"),
				        "tuple snapshot Console remains inspectable under AI");
				snapshot_world.tick();
				require(controller(snapshot_world, 101, "propulsion").at("commands").at("cruise").at("value") == 1000,
				        "tree tuple reads freeze before same-tick writes");
				snapshot_world.tick();
				require(controller(snapshot_world, 101, "propulsion").at("commands").at("cruise").at("value") == 2000,
				        "accepted tuple becomes visible next decision tick");
				const auto snapshot_view = snapshot_world.observe(101);
				for (const auto &system : snapshot_view->systems)
					if (system.id == "port" || system.id == "starboard")
						require(system.requested == 2000 && system.telemetry.at("navigating") == 1,
						        "cruise speed updates preserve active destination guidance");
			}
		}
		World prepared;
		auto receipt_source = controller_fixture();
		auto &receipt_owner = receipt_source["entities"][0]["control_layout"]["controllers"][0];
		receipt_owner.erase("fallback");
		receipt_owner["tree"] = "test.receipts";
		receipt_owner["commands"].push_back(
		    {{"id", "held"}, {"action", "longitudinal_thrust"}, {"consumers", {"port"}}});
		receipt_source["trees"] = Json::parse(R"json([{"id":"test.receipts","context":"controller","initial":"run",
		 "command_bindings":[{"channel":"speed"},{"channel":"held"}],
		 "states":[{"id":"run","running":[{"action":"write.held","arguments":[500]},
		 {"action":"write.speed","arguments":[{"observation":"command.held.consumed"}]}]}]}])json");
		World receipt_world;
		const auto receipt_catalog = load(receipt_source);
		receipt_world.load(receipt_catalog, "test.map");
		require(receipt_world.prepare_console_profile({1, "drive"}, "automatic") &&
		            receipt_world.claim(101, 1, "drive"),
		        "receipt fixture grants AI Controller with human observation");
		const auto initial_consumed =
		    controller(receipt_world, 101, "propulsion").at("commands").at("held").at("consumed");
		receipt_world.tick();
		const auto refused_receipt = controller(receipt_world, 101, "propulsion").at("commands").at("held");
		require(!refused_receipt.at("accepted").get<bool>() && !refused_receipt.at("pending").get<bool>() &&
		            refused_receipt.at("consumed") == refused_receipt.at("revision") &&
		            controller(receipt_world, 101, "propulsion").at("commands").at("speed").at("value") ==
		                initial_consumed,
		        "physical refusal records consumed receipt while same-tick tree reads retain previous receipt");
		receipt_world.tick();
		require(controller(receipt_world, 101, "propulsion").at("commands").at("speed").at("value") ==
		            refused_receipt.at("consumed"),
		        "next decision sees consumed physical refusal receipt");
		World receipt_resumed;
		receipt_resumed.restore(receipt_catalog, receipt_world.save());
		receipt_world.tick();
		receipt_resumed.tick();
		require(receipt_world.save() == receipt_resumed.save(),
		        "receipt-driven Controller continuation restores exactly");
		prepared.load(content, "test.map");
		require(!prepared.prepare_console_profile({1, "drive"}, "missing") &&
		            !prepared.prepare_console_profile({1, "absent"}, "automatic"),
		        "preparation validates authored Console and complexity identities");
		require(prepared.prepare_console_profile({1, "drive"}, "automatic"),
		        "unclaimed tick-zero World accepts selected Console complexity");
		require(prepared.claim(101, 1, "drive") && !controller(prepared, 101, "propulsion").at("human").get<bool>(),
		        "prepared AI allocation survives initial human Console claim");
		require(!prepared.prepare_console_profile({1, "drive"}, "direct"),
		        "preparation cannot bypass authenticated command path after a claim");
		prepared.tick();
		require(!prepared.prepare_console_profile({1, "drive"}, "direct"),
		        "preparation cannot change a running simulation profile");
		World prepared_restore;
		prepared_restore.restore(content, prepared.save());
		prepared_restore.clear_claims();
		require(prepared_restore.claim(101, 1, "drive") &&
		            !controller(prepared_restore, 101, "propulsion").at("human").get<bool>(),
		        "saved prepared complexity survives recovery and a new human claim");
		World world;
		world.load(content, "test.map");
		require(world.claim(101, 1, "drive") && world.claim(102, 1, "observer"), "Consoles claim independently");
		auto command = speed(world, 101, "propulsion", 5000);
		auto scalar_with_tuple = command;
		scalar_with_tuple.arguments = {1};
		require(!world.submit(101, scalar_with_tuple), "scalar command rejects unexpected compound arguments");
		require(!world.submit(102, command), "another Console cannot write a Controller section");
		auto foreign = command;
		foreign.target = 999;
		require(!world.submit(101, foreign), "Controller identity never authorises a foreign entity");
		require(world.submit(101, command), "own Controller accepts a valid human proposal");
		world.tick();
		require(requested(world, "port") == 5000 && requested(world, "starboard") == 5000,
		        "one committed setting reaches both physical engines");
		require(controller(world, 101, "propulsion").at("commands").at("speed").at("consumers") ==
		            Json::array({"port", "starboard"}),
		        "shared Controller projection exposes exact physical subscriptions for cards");
		require(controller(world, 101, "propulsion").at("commands").at("speed").at("value") == 5000,
		        "accepted intent is exposed separately from physical observations");

		const auto old_command = speed(world, 101, "propulsion", 9000);
		require(world.submit(101, old_command) && world.submit(101, {1, "preset", 1, {}}),
		        "queued command and profile transition are admitted before commit");
		world.tick();
		require(!controller(world, 101, "propulsion").at("human").get<bool>(), "profile delegates the Controller");
		require(requested(world, "port") == 5000 && !world.submit(101, old_command),
		        "handover invalidates obsolete writes without erasing accepted settings");
		require(world.submit(101, {1, "preset", 0, {}}), "Console returns to selected direct profile");
		world.tick();
		require(!world.submit(101, old_command), "previous human generation remains invalid after return");
		require(world.submit(101, speed(world, 101, "propulsion", 7000)), "fresh operator generation can write");
		world.tick();

		const auto saved = world.save();
		require(saved.at("schema") == 7, "Controller continuation has an explicit new save schema");
		World resumed;
		resumed.restore(content, saved);
		for (int tick = 0; tick < 12; ++tick)
		{
			world.tick();
			resumed.tick();
		}
		require(world.save() == resumed.save(), "shared settings and physical continuation survive restore");
		auto forged = saved;
		forged["bodies"][0]["console_controllers"]["controllers"][0]["human"] = false;
		const auto before_refusal = resumed.save();
		rejects(
		    [&]
		    {
			    resumed.restore(content, forged);
		    },
		    "saved operator must agree with claims and profile");
		require(resumed.save() == before_refusal, "malformed Controller restore is transactional");

		World disconnected;
		disconnected.restore(content, saved);
		auto obsolete = speed(disconnected, 101, "propulsion", 11000);
		require(disconnected.submit(101, obsolete), "human command queues before recovery clears claims");
		disconnected.clear_claims();
		disconnected.tick();
		require(disconnected.claim(101, 1, "drive"), "Console can acquire a fresh claim after recovery");
		require(requested(disconnected, "port") == 7000 && !disconnected.submit(101, obsolete),
		        "clearing claims cancels queued writes and resets the effective operator generation");
		require(disconnected.submit(101, {1, "away", 1, {}}), "Away is a Console availability transition");
		disconnected.tick();
		require(!controller(disconnected, 101, "propulsion").at("human").get<bool>(), "Away uses ordinary AI");
		require(disconnected.submit(101, {1, "away", 0, {}}), "human can return from Away");
		disconnected.tick();
		require(controller(disconnected, 101, "propulsion").at("human").get<bool>() &&
		            requested(disconnected, "port") == 7000,
		        "return restores profile allocation and accepted setting");

		World split;
		split.load(load(controller_fixture(true)), "test.map");
		require(split.claim(101, 1, "drive"), "split throttle Console claims through ordinary route");
		require(split.submit(101, speed(split, 101, "port_drive", 3000)) &&
		            split.submit(101, speed(split, 101, "starboard_drive", 8000)),
		        "separate sections accept separate intent");
		split.tick();
		require(requested(split, "port") == 3000 && requested(split, "starboard") == 8000,
		        "independent Controllers cannot overwrite each other's throttle");

		auto constrained_source = controller_fixture();
		constrained_source["entities"][0]["resources"] = Json::parse(R"json({"stores":[
		 {"id":"empty","capacity":1000,"initial":0,"charge_rate":1000,"discharge_rate":1000},
		 {"id":"charged","capacity":1000,"initial":1000,"charge_rate":1000,"discharge_rate":1000}]})json");
		for (const auto *engine : {"port", "starboard"})
		{
			constrained_source["entities"][0]["systems"][engine]["power_store"] =
			    std::string(engine) == "port" ? "empty" : "charged";
			constrained_source["entities"][0]["systems"][engine]["power_rate"] = 100;
		}
		World constrained;
		constrained_source["entities"][0]["systems"]["port"]["powerless_start"] = true;
		constrained.load(load(constrained_source), "test.map");
		require(constrained.claim(101, 1, "drive"), "constrained physical consumer claims");
		require(constrained.submit(101, speed(constrained, 101, "propulsion", 5000)),
		        "one setting drives both constrained engines");
		std::int64_t paid = 0;
		for (int tick = 0; tick < 5; ++tick)
		{
			constrained.tick();
			const auto projection = constrained.observe(101);
			for (const auto &system : projection->systems)
			{
				require(system.requested == 5000,
				        "each consumer retains the same accepted setting despite local supply");
				if (system.id == "port")
					require(system.telemetry.at("supply_permille") == 0 &&
					            system.telemetry.at("actual_longitudinal") == 0 &&
					            system.telemetry.at("used_power") == 0,
					        "exhausted consumer cannot borrow another engine's grant");
				else
				{
					require(system.telemetry.at("actual_longitudinal") > 0,
					        "independently supplied engine still produces thrust");
					paid += system.telemetry.at("used_power") / World::ticks_per_second;
					require(1000 - system.telemetry.at("stored_energy") == paid,
					        "physical consumption conserves its own reserve");
				}
			}
		}
		auto ai_slot_source = controller_session_fixture();
		ai_slot_source["entities"][0]["control_layout"]["default_profile"] = "automatic";
		ai_slot_source["entities"][0]["control_layout"]["controllers"][0]["tree"] = "test.cruise";
		ai_slot_source["trees"] = Json::parse(R"json([{ "id":"test.cruise","context":"controller","initial":"run",
		 "command_bindings":[{"channel":"speed"}],"states":[{"id":"run","running":[
		 {"action":"write.speed","arguments":[5000]}]}]}])json");
		auto ai_free_source = ai_slot_source;
		ai_free_source["maps"] = controller_fixture()["maps"];
		World ai_slot, ai_free;
		ai_slot.load(load(ai_slot_source), "test.map", {{"test.slot", "test.controllable"}});
		ai_free.load(load(ai_free_source), "test.map");
		for (int tick = 0; tick < 20; ++tick)
		{
			ai_slot.tick();
			ai_free.tick();
			const auto slot_body = ai_slot.inspect().front(), free_body = ai_free.inspect().front();
			require(slot_body.x == free_body.x && slot_body.y == free_body.y && slot_body.vx == free_body.vx &&
			            slot_body.vy == free_body.vy && slot_body.heading == free_body.heading,
			        "unclaimed all-AI entity has identical control trajectory inside and outside player slot");
		}
		auto request_source = controller_fixture();
		request_source["entities"][0]["systems"]["port"]["impulse"] = {{"charge_ticks", 20}};
		request_source["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "impulse"}, {"action", "impulse"}, {"consumers", {"port"}}});
		const auto request_content = load(request_source);
		World requests;
		requests.load(request_content, "test.map");
		require(requests.claim(101, 1, "drive"), "request fixture claims the ordinary Console");
		auto request = speed(requests, 101, "propulsion", 1);
		request.channel = "impulse";
		request.request_id = 1;
		require(requests.submit(101, request) && requests.submit(101, request),
		        "duplicate request identity is idempotent");
		auto concurrent = request;
		concurrent.request_id = 2;
		require(!requests.submit(101, concurrent), "a second outstanding request cannot silently replace the first");
		requests.tick();
		const auto receipt = controller(requests, 101, "propulsion").at("commands").at("impulse");
		require(receipt.at("accepted").get<bool>(), "request reaches its single physical executor");
		require(requests.submit(101, request), "completed request retry returns its prior outcome");
		requests.tick();
		require(controller(requests, 101, "propulsion").at("commands").at("impulse").at("revision") ==
		            receipt.at("revision"),
		        "retry does not create a second physical request revision");
		World request_resumed;
		request_resumed.restore(request_content, requests.save());
		require(request_resumed.submit(101, request), "request identity survives restore");
		request_resumed.tick();
		requests.tick();
		require(request_resumed.save() == requests.save(), "restored retry cannot restart physical work");

		// An autonomous generic entity uses a real decision program and the same command board as a human.
		World automatic, human;
		auto ai_source = controller_fixture();
		ai_source["trees"] = Json::parse(R"json([
  {
    "id": "test.propulsion",
    "context": "controller",
    "initial": "run",
    "command_bindings": [
      {
        "channel": "speed",
        "type": "integer",
        "semantics": "setting"
      }
    ],
    "states": [
      {
        "id": "run",
        "running": [
          {
            "action": "write.speed",
            "arguments": [
              5000
            ],
            "once": true
          }
        ],
        "transitions": [
          {
            "target": "run",
            "conditions": [
              {
                "left": {
                  "elapsed": true
                },
                "op": "ge",
                "right": 10
              }
            ]
          }
        ]
      }
    ]
  }
])json");
		auto &ai_controller = ai_source["entities"][0]["control_layout"]["controllers"][0];
		ai_controller.erase("fallback");
		ai_controller["tree"] = "test.propulsion";
		const auto ai_content = load(ai_source);
		automatic.load(ai_content, "test.map");
		human.load(content, "test.map");
		require(human.claim(101, 1, "drive") && human.submit(101, speed(human, 101, "propulsion", 5000)),
		        "equivalent human command admitted");
		for (int tick = 0; tick < 20; ++tick)
		{
			automatic.tick();
			human.tick();
			const auto a = automatic.inspect().at(0), b = human.inspect().at(0);
			require(a.x == b.x && a.y == b.y && a.vx == b.vx && a.vy == b.vy,
			        "autonomous and human-operated entities use identical bound physical execution");
		}
		World ai_resumed;
		ai_resumed.restore(ai_content, automatic.save());
		for (int tick = 0; tick < 11; ++tick)
		{
			automatic.tick();
			ai_resumed.tick();
		}
		if (automatic.save() != ai_resumed.save())
			std::cerr << Json::diff(automatic.save(), ai_resumed.save()).dump(2) << '\n';
		require(automatic.save() == ai_resumed.save(),
		        "Controller decision continuation resumes independently of equipment");

		{
			auto cyclic = ai_source;
			cyclic["entities"][0]["motion_model"] = "vector";
			cyclic["trees"][0]["initial"] = "short";
			cyclic["trees"][0]["states"] = Json::parse(R"json([
			 {"id":"short","running":[{"action":"write.speed","arguments":[1000],"once":true}],
			  "transitions":[{"target":"long","conditions":[{"left":{"elapsed":true},"op":"ge","right":1}]}]},
			 {"id":"long","running":[{"action":"write.speed","arguments":[3000],"once":true}],
			  "transitions":[{"target":"short","conditions":[{"left":{"elapsed":true},"op":"ge","right":3}]}]}
			])json");
			const auto cyclic_content = load(cyclic);
			World cycle;
			cycle.load(cyclic_content, "test.map");
			for (const auto expected : {1000, 3000, 3000, 3000, 1000, 3000})
			{
				World resumed;
				resumed.restore(cyclic_content, cycle.save());
				cycle.tick();
				resumed.tick();
				require(cycle.save().at("bodies")[0].at("systems").at("port").at("speed").get<std::int64_t>() ==
				            expected,
				        "cycle emits on the next tick, including one-tick states");
				require(cycle.save() == resumed.save(), "cycle resumes exactly at every transition boundary");
			}
			const auto saved = cycle.save();
			auto retired = saved;
			retired["schema"] = 6;
			phoenix::test::rejected(
			    [&]
			    {
				    cycle.restore(cyclic_content, retired);
			    },
			    "start a new session");
			require(cycle.save() == saved, "retired saves cannot replace a live world");
			require(cycle.claim(101, 1, "drive") && cycle.submit(101, speed(cycle, 101, "propulsion", 5000)),
			        "human interrupts cyclic Controller");
			cycle.tick();
			require(cycle.save().at("bodies")[0].at("systems").at("port").at("speed").get<std::int64_t>() == 5000 &&
			            cycle.release(101),
			        "human owns accepted setting until release");
			cycle.tick();
			require(cycle.save().at("bodies")[0].at("systems").at("port").at("speed").get<std::int64_t>() == 1000,
			        "handover restarts the ordinary tree at its initial state");
		}

		auto contextual_source = controller_fixture();
		contextual_source["trees"] = Json::parse(R"json([
		    {"id":"test.contextual","context":"controller","initial":"root",
		     "command_bindings":[{"channel":"speed","type":"integer","semantics":"setting"}],
		     "memory":[{"name":"remembered","type":"integer","initial":5000}],
		     "states":[
		       {"id":"root","initial":"operate"},
		       {"id":"operate","parent":"root","running":[
		         {"action":"write.speed","arguments":[{"memory":"remembered"}]},
		         {"memory":"remembered","value":{"intent":"speed"}}
		       ]}
		     ]}
		])json");
		auto &contextual_definition = contextual_source["entities"][0]["control_layout"]["controllers"][0];
		contextual_definition.erase("fallback");
		contextual_definition["tree"] = "test.contextual";
		const auto contextual_content = load(contextual_source);
		World contextual, direct;
		contextual.load(contextual_content, "test.map");
		direct.load(content, "test.map");
		require(direct.claim(101, 1, "drive") && direct.submit(101, speed(direct, 101, "propulsion", 5000)),
		        "human supplies same setting as contextual Controller");
		for (int tick = 0; tick < 5; ++tick)
		{
			contextual.tick();
			direct.tick();
			const auto a = contextual.inspect().at(0), b = direct.inspect().at(0);
			require(a.x == b.x && a.y == b.y && a.vx == b.vx && a.vy == b.vy,
			        "hierarchical Controller AI and human use equivalent admitted commands");
		}
		const auto memory = contextual.save()
		                        .at("bodies")[0]
		                        .at("console_controllers")
		                        .at("controllers")[0]
		                        .at("brain")
		                        .at("instance")
		                        .at("memory");
		require(contextual.claim(101, 1, "drive"), "human takes over contextual Controller");
		require(!controller(contextual, 101, "propulsion").contains("memory"),
		        "private AI memory stays out of Console projection");
		require(contextual.submit(101, speed(contextual, 101, "propulsion", 7000)),
		        "human writes after tree interruption");
		contextual.tick();
		require(requested(contextual, "port") == 7000, "interrupted AI cannot overwrite human control");
		require(contextual.save()
		                .at("bodies")[0]
		                .at("console_controllers")
		                .at("controllers")[0]
		                .at("brain")
		                .at("instance")
		                .at("memory") == memory,
		        "human operation preserves the Controller's private memory");
		World contextual_resumed;
		contextual_resumed.restore(contextual_content, contextual.save());
		require(contextual.release(101) && contextual_resumed.release(101), "ordinary AI resumes on release");
		for (int tick = 0; tick < 5; ++tick)
		{
			contextual.tick();
			contextual_resumed.tick();
		}
		require(contextual.save() == contextual_resumed.save(),
		        "interrupted hierarchical continuation survives restore");

		auto observations_source = controller_fixture(true);
		observations_source["trees"] = Json::parse(R"json([
		    {"id":"test.writer","context":"controller","initial":"run",
		     "command_bindings":[{"channel":"speed"}],
		     "states":[{"id":"run","running":[{"action":"write.speed","arguments":[5000]}]}]},
		    {"id":"test.reader","context":"controller","initial":"run",
		     "command_bindings":[{"channel":"speed"}],
		     "system_bindings":[{"name":"port_requested","system":"port","field":"requested","type":"integer"}],
		     "states":[{"id":"run","running":[{"action":"write.speed","arguments":[{"observation":"port_requested"}]}]}]}
		])json");
		auto &observation_layout = observations_source["entities"][0]["control_layout"];
		observation_layout["default_profile"] = "automatic";
		observation_layout["controllers"][0].erase("fallback");
		observation_layout["controllers"][0]["tree"] = "test.writer";
		observation_layout["controllers"][1].erase("fallback");
		observation_layout["controllers"][1]["tree"] = "test.reader";
		World observations;
		observations.load(load(observations_source), "test.map");
		require(observations.claim(101, 1, "drive") && observations.claim(102, 1, "observer"),
		        "human can observe a fully AI Console without changing its profile");
		const auto before_tick = requested(observations, "port");
		observations.tick();
		require(requested(observations, "port") == 5000 && requested(observations, "starboard") == before_tick,
		        "later Controller sees frozen pre-execution observations, not earlier Controller's physical effects");
		const auto human_port = requested(observations, "port");
		observations.tick();
		require(requested(observations, "starboard") == human_port,
		        "Controller reads the same permitted System observation presented to the human");
		require(observations.observe(102)->systems.empty() && observations.observe(102)->contacts.empty(),
		        "another Console does not inherit physical observations from a co-hosted Console");
		auto foreign_feed = observations_source;
		foreign_feed["entities"][0]["stations"][0]["consoles"][0]["feeds"] = {"starboard"};
		rejects(
		    [&]
		    {
			    load(foreign_feed);
		    },
		    "Controller System observation must belong to the Console's feed roster");
		auto wrong_type = observations_source;
		wrong_type["trees"][1]["system_bindings"][0]["type"] = "boolean";
		wrong_type["trees"][1]["states"][0]["running"][0]["arguments"] = {5000};
		rejects(
		    [&]
		    {
			    load(wrong_type);
		    },
		    "System observation type cannot reinterpret requested speed as a boolean");
		auto unavailable_observation = observations_source;
		unavailable_observation["trees"][1]["system_bindings"][0]["field"] = "heat";
		rejects(
		    [&]
		    {
			    load(unavailable_observation);
		    },
		    "Catalog refuses telemetry absent from actual fitting");
		auto resource_observations = observations_source;
		resource_observations["entities"][0]["resources"] = Json::parse(R"json({"stores":[
		 {"id":"cell","capacity":100000,"initial":50000,"charge_rate":100000,
		  "discharge_rate":100000,"stop_heat":100000,"recover_heat":50000}]})json");
		resource_observations["entities"][0]["systems"]["port"]["power_store"] = "cell";
		resource_observations["entities"][0]["resources"]["coolant_sources"] =
		    Json::parse(R"json([{"id":"tank","initial":100,"removal_rate":40,"coolant_per_heat_ppm":1000000}])json");
		resource_observations["entities"][0]["resources"]["cooling_links"] = Json::parse(
		    R"json([{"id":"feed","source":"tank","store":"cell","capacity_rate":40,"allocation_rate":40}])json");
		resource_observations["entities"][0]["systems"]["cooler"] = {{"kind", "coolant"}, {"cooling_link", "feed"}};
		resource_observations["entities"][0]["stations"][0]["consoles"][0]["feeds"].push_back("cooler");
		resource_observations["trees"][1]["system_bindings"] = Json::parse(R"json([
		 {"name":"port_requested","system":"port","field":"heat","type":"integer"},
		 {"name":"thermal","system":"port","field":"thermal_stopped","type":"boolean"},
		 {"name":"following","system":"port","field":"following","type":"boolean"},
		 {"name":"forecast","system":"port","field":"thermal_recovery_ticks","type":"integer"},
		 {"name":"coolant_forecast","system":"cooler","field":"coolant_depletion_ticks","type":"integer"},
		 {"name":"coolant_used","system":"cooler","field":"actual_coolant_used","type":"integer"}])json");
		const auto resource_catalog = load(resource_observations);
		World resource_view;
		resource_view.load(resource_catalog, "test.map");
		require(resource_view.claim(101, 1, "drive"), "resource observation Console claim");
		resource_view.tick();
		const auto fitted_schemas = compile_system_observations(resource_catalog, "test.controllable");
		require(fitted_schemas.at("port").contains("heat") && !fitted_schemas.at("starboard").contains("heat"),
		        "store telemetry belongs only to fitted resource binding");
		const auto resource_projection = resource_view.observe(101);
		for (const auto &system : resource_projection->systems)
		{
			const auto &schema = fitted_schemas.at(system.id);
			require(schema.size() == system.telemetry.size(),
			        "compiled schema and produced telemetry have equal fields");
			for (const auto &[field, value] : system.telemetry)
				require(schema.contains(field), "compiled fitting contract includes each produced telemetry field");
		}
		// A synthetic generator exposes committed load through the same typed feed
		// used by demand-following Engineering policies; no shipped tuning is loaded.
		auto demand_source = resource_observations;
		auto &demand_ship = demand_source["entities"][0];
		demand_ship["systems"]["generator"] = {{"kind", "generator"}, {"generator", "source"}};
		demand_ship["resources"]["generators"] = Json::parse(R"json([
		 {"id":"source","store":"cell","maximum_rate":700,"response_rate":700,"initial_output":100}])json");
		demand_ship["resources"]["stores"][0]["loss_full_ppm"] = 2000;
		demand_ship["stations"][0]["consoles"][0]["feeds"].push_back("generator");
		demand_source["trees"][1]["system_bindings"][0] = {
		    {"name", "port_requested"}, {"system", "generator"}, {"field", "demand_output"}, {"type", "integer"}};
		World demand_world;
		demand_world.load(load(demand_source), "test.map");
		require(demand_world.claim(101, 1, "drive"), "claim demand-following observation Console");
		demand_world.tick();
		std::int64_t last_demand = -1;
		const auto demand_view = demand_world.observe(101);
		for (const auto &system : demand_view->systems)
			if (system.id == "generator")
				last_demand = system.telemetry.at("demand_output");
		require(last_demand > 0 && last_demand <= 700, "generator demand includes losses and respects fitted maximum");
		demand_world.tick();
		require(requested(demand_world, "starboard") == last_demand,
		        "AI reads committed generator demand through its permitted feed");
		auto wrong_resource_type = resource_observations;
		wrong_resource_type["trees"][1]["system_bindings"][1]["type"] = "integer";
		rejects(
		    [&]
		    {
			    load(wrong_resource_type);
		    },
		    "Catalog preserves boolean thermal flag type");
		auto bounded_cooling = resource_observations;
		bounded_cooling["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "cooling"}, {"action", "cooling_allocation"}, {"consumers", {"cooler"}}});
		const auto cooling_layout = compile_controllable_layout(load(bounded_cooling), "test.controllable");
		require(cooling_layout.controllers[0].commands.back().maximum == 40,
		        "shared command bound derives from actual coolant source/link capacity");
		bounded_cooling["entities"][0]["control_layout"]["controllers"][0]["commands"].back()["maximum"] = 41;
		rejects(
		    [&]
		    {
			    load(bounded_cooling);
		    },
		    "authored coolant command cannot exceed fitted capacity");
		auto phantom_resource = controller_fixture();
		phantom_resource["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "overdrive"}, {"action", "overdrive"}, {"consumers", {"port"}}});
		rejects(
		    [&]
		    {
			    load(phantom_resource);
		    },
		    "engine without a resource store cannot accept resource mode command");
		auto nonowner_mode = resource_observations;
		nonowner_mode["entities"][0]["systems"]["storage"] = {{"kind", "storage"}, {"power_store", "cell"}};
		nonowner_mode["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "overdrive"}, {"action", "overdrive"}, {"consumers", {"port"}}});
		rejects(
		    [&]
		    {
			    load(nonowner_mode);
		    },
		    "resource consumer cannot take store mode authority from dedicated storage fitting");
		auto unsafe_held = controller_fixture();
		unsafe_held["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "held"},
		     {"action", "longitudinal_thrust"},
		     {"minimum", 1},
		     {"initial", 1},
		     {"consumers", {"port"}}});
		rejects(
		    [&]
		    {
			    load(unsafe_held);
		    },
		    "held input range must permit neutral authority handover");
		auto active_request = request_source;
		active_request["entities"][0]["control_layout"]["controllers"][0]["commands"][1]["initial"] = 1;
		rejects(
		    [&]
		    {
			    load(active_request);
		    },
		    "one-shot request must start idle without synthetic execution");
		auto command_read_source = observations_source;
		command_read_source["trees"][1].erase("system_bindings");
		command_read_source["trees"][1]["command_reads"] = Json::array(
		    {{{"name", "port_requested"}, {"controller", "port_drive"}, {"channel", "speed"}, {"type", "integer"}}});
		World command_reads;
		command_reads.load(load(command_read_source), "test.map");
		require(command_reads.claim(101, 1, "drive"), "Console can observe shared Controller intent");
		command_reads.tick();
		require(controller(command_reads, 101, "port_drive").at("commands").at("speed").at("value") == 5000 &&
		            controller(command_reads, 101, "starboard_drive").at("commands").at("speed").at("value") == 0,
		        "one Controller cannot read another Controller's same-tick command write");
		command_reads.tick();
		require(controller(command_reads, 101, "starboard_drive").at("commands").at("speed").at("value") == 5000,
		        "same-Console command read receives prior committed intent");
		auto bad_command_read = command_read_source;
		bad_command_read["trees"][1]["command_reads"][0]["channel"] = "unknown";
		rejects(
		    [&]
		    {
			    load(bad_command_read);
		    },
		    "Controller read must reference an existing command channel");
		bad_command_read = command_read_source;
		bad_command_read["trees"][1]["command_reads"][0]["type"] = "boolean";
		bad_command_read["trees"][1]["states"][0]["running"][0]["arguments"] = {5000};
		rejects(
		    [&]
		    {
			    load(bad_command_read);
		    },
		    "Controller read cannot reinterpret the channel type");
		bad_command_read = command_read_source;
		bad_command_read["entities"][0]["control_layout"]["controllers"][1]["console"] = "observer";
		for (auto &profile : bad_command_read["entities"][0]["control_layout"]["profiles"])
		{
			const auto key = profile["id"] == "direct" ? "human" : "ai";
			profile["consoles"]["drive"][key] = {"port_drive"};
			profile["consoles"]["observer"][key] = {"starboard_drive"};
		}
		rejects(
		    [&]
		    {
			    load(bad_command_read);
		    },
		    "co-hosted Consoles cannot read each other's private command sections");
		auto forbidden = contextual_source;
		forbidden["trees"][0]["command_bindings"].push_back({{"channel", "foreign"}, {"type", "integer"}});
		rejected(
		    [&]
		    {
			    load(forbidden);
		    },
		    "unowned command channel");
		forbidden = contextual_source;
		forbidden["trees"][0]["states"][1]["running"][0]["arguments"] = Json::array({{{"observation", "self.x"}}});
		rejects(
		    [&]
		    {
			    load(forbidden);
		    },
		    "Controller trees cannot acquire direct physical observations");
		forbidden = contextual_source;
		forbidden["trees"][0]["context"] = "scenario";
		rejects(
		    [&]
		    {
			    load(forbidden);
		    },
		    "scenario context cannot operate a Console Controller");
		forbidden = contextual_source;
		forbidden["knowledge"] = Json::parse(R"json({
		    "roles":["drive","private"],
		    "schemas":[{"id":"test.secret","producers":["private"],"readers":["private"],
		      "fields":[{"id":"amount","type":"integer"}]}]
		})json");
		forbidden["entities"][0]["stations"][0]["consoles"][0]["knowledge_roles"] = {"drive"};
		forbidden["trees"][0]["record_bindings"] =
		    Json::array({{{"name", "secret"}, {"scope", "console"}, {"schema", "test.secret"}, {"field", "amount"}}});
		rejected(
		    [&]
		    {
			    load(forbidden);
		    },
		    "record binding forbidden to owning Console");
		forbidden["trees"][0]["record_bindings"][0]["scope"] = "ship";
		rejects(
		    [&]
		    {
			    load(forbidden);
		    },
		    "entity-wide read cannot bypass private-field publication policy");

		auto messages_source = controller_fixture(true);
		messages_source["schema"] = 4;
		messages_source["knowledge"] = Json::parse(R"json({"roles":["crew"],"schemas":[
		 {"id":"request","producers":["crew"],"readers":["crew"],"fields":[{"id":"amount","type":"integer"}]}]})json");
		auto &message_entity = messages_source["entities"][0];
		for (auto &console : message_entity["stations"][0]["consoles"])
			console["knowledge_roles"] = {"crew"};
		auto &message_layout = message_entity["control_layout"];
		message_layout["controllers"][0]["id"] = "port";
		message_layout["controllers"][1]["id"] = "port";
		message_layout["controllers"][1]["console"] = "observer";
		message_layout["profiles"] = Json::parse(R"json([
		 {"id":"direct","consoles":{"drive":{"human":["port"]},"observer":{"human":["port"]}}},
		 {"id":"automatic","consoles":{"drive":{"ai":["port"]},"observer":{"ai":["port"]}}}])json");
		message_entity["message_templates"] = {"test.request"};
		messages_source["messages"] = Json::parse(R"json([
		 {"id":"test.request","schema":"request","title_key":"test.request.title","body_key":"test.request.body",
		 "destination":{"kind":"controller","console":"drive","id":"port"},"delay_ticks":3,"expires_after_ticks":30,
		 "fields":{"amount":7},"parameters":{},"automatic":false}])json");
		const auto message_content = load(messages_source);
		World messaging;
		messaging.load(message_content, "test.map");
		require(messaging.claim(101, 1, "drive") && messaging.claim(102, 1, "observer"), "message Consoles claimed");
		require(messaging.submit(101, {1, "preset", 1, {}}), "recipient delegates before send");
		messaging.tick();
		OperatingCommand send{1, "message.test.request", 7, {}};
		send.controller = "port";
		send.generation = controller(messaging, 102, "port").at("generation").get<std::uint64_t>();
		require(messaging.submit(102, send), "human Controller sends permitted intent");
		messaging.tick();
		require(messaging.observe(101)->gameplay.at("messages").empty(), "Controller delivery respects authored delay");
		World message_resume;
		message_resume.restore(message_content, messaging.save());
		for (auto *run : {&messaging, &message_resume})
		{
			require(run->submit(101, {1, "preset", 0, {}}), "recipient becomes human before delivery");
			bool notified = false;
			for (int tick = 0; tick < 4; ++tick)
			{
				run->tick();
				const auto view = run->observe(101)->gameplay;
				for (const auto &notice : view.at("notifications"))
					if (!view.at("messages").empty() && notice.at("sequence") == view.at("messages")[0].at("sequence"))
						notified = true;
			}
			require(notified, "delivery notifies the current human Controller operator");
		}
		const auto delivered = messaging.observe(101)->gameplay.at("messages");
		require(delivered.size() == 1 && delivered[0].at("payload").at("fields").at("amount") == 7,
		        "Controller-addressed message reaches its current human Console");
		require(messaging.observe(102)->gameplay.at("messages").empty(),
		        "same Controller name in another Console cannot intercept the message");
		require(message_resume.observe(101)->gameplay.at("messages") == delivered &&
		            message_resume.save() == messaging.save(),
		        "pending Controller message identity and routing survive restoration and handover");
		const auto bindings = messaging.save().at("knowledge").at("ships")[0].at("controllers");
		require(bindings.at("drive/port") != bindings.at("observer/port"),
		        "Controller actor identity includes Console");
		World stale_sender;
		stale_sender.load(message_content, "test.map");
		require(stale_sender.claim(101, 1, "drive") && stale_sender.claim(102, 1, "observer"), "stale sender claims");
		send.generation = controller(stale_sender, 102, "port").at("generation").get<std::uint64_t>();
		require(stale_sender.submit(102, {1, "preset", 1, {}}) && stale_sender.submit(102, send),
		        "message admitted behind an uncommitted profile transition");
		for (int tick = 0; tick < 5; ++tick)
			stale_sender.tick();
		require(stale_sender.observe(101)->gameplay.at("messages").empty(),
		        "execution rechecks message sender generation after profile transition");
		auto ai_messages_source = messages_source;
		ai_messages_source["messages"][0]["fields"]["amount"] = {{"argument", "amount"}};
		ai_messages_source["trees"] = Json::parse(R"json([
		 {"id":"test.sender","context":"controller","initial":"send","message_templates":["test.request"],
		  "states":[{"id":"send","entry":[{"action":"send.test.request","arguments":[6000,false,"intent"]}]}]},
		 {"id":"test.receiver","context":"controller","initial":"wait","command_bindings":[{"channel":"speed"}],
		  "message_bindings":[{"name":"amount","schema":"request","field":"amount"}],
		  "states":[{"id":"wait","transitions":[{"target":"received","conditions":[
		    {"left":{"observation":"message.available"},"op":"eq","right":true}]}]},
		   {"id":"received","entry":[{"action":"write.speed","arguments":[{"observation":"amount"}]},
		    {"action":"acknowledge_message","arguments":[{"observation":"message.id"}]}]}]}
		])json");
		ai_messages_source["entities"][0]["control_layout"]["controllers"][0]["tree"] = "test.receiver";
		ai_messages_source["entities"][0]["control_layout"]["controllers"][1]["tree"] = "test.sender";
		const auto ai_message_content = load(ai_messages_source);
		World ai_messages;
		ai_messages.load(ai_message_content, "test.map");
		require(ai_messages.claim(101, 1, "drive") && ai_messages.claim(102, 1, "observer"), "AI message claims");
		require(ai_messages.submit(102, {1, "preset", 1, {}}), "sender delegates to AI");
		for (int tick = 0; tick < 6; ++tick)
			ai_messages.tick();
		require(ai_messages.observe(101)->gameplay.at("messages").size() == 1 && requested(ai_messages, "port") == 0,
		        "AI sends once while human recipient retains command control");
		require(ai_messages.observe(102)->gameplay.at("messages").empty(), "same-named sender has no recipient inbox");
		World ai_message_resume;
		ai_message_resume.restore(ai_message_content, ai_messages.save());
		for (auto *run : {&ai_messages, &ai_message_resume})
		{
			require(run->submit(101, {1, "preset", 1, {}}), "human yields with delivered message history intact");
			for (int tick = 0; tick < 4; ++tick)
				run->tick();
			require(requested(*run, "port") == 6000 && run->observe(101)->gameplay.at("messages").size() == 1,
			        "recipient AI reads typed payload and acknowledges without duplicating message effects");
		}
		require(ai_messages.save() == ai_message_resume.save(),
		        "AI send/receive continuation survives handover restore");
		const auto consumed = ai_messages.save();
		const auto receiver_id = consumed.at("knowledge").at("ships")[0].at("controllers").at("drive/port");
		bool acknowledged = false;
		for (const auto &memory : consumed.at("knowledge").at("runtime").at("memory"))
			if (memory.at("id") == receiver_id)
				acknowledged =
				    memory.at("cursor") == ai_messages.observe(101)->gameplay.at("messages")[0].at("sequence");
		require(acknowledged, "acknowledge action advances recipient Controller continuation");
		World consumed_resume;
		consumed_resume.restore(ai_message_content, consumed);
		for (int tick = 0; tick < 4; ++tick)
		{
			ai_messages.tick();
			consumed_resume.tick();
		}
		require(ai_messages.save() == consumed_resume.save(),
		        "acknowledged inbox and effect journal survive restoration");
		auto forbidden_message = ai_messages_source;
		forbidden_message["entities"][0]["message_templates"] = Json::array();
		rejects(
		    [&]
		    {
			    load(forbidden_message);
		    },
		    "Controller cannot send template absent from entity authority");
		forbidden_message = ai_messages_source;
		forbidden_message["entities"][0]["stations"][0]["consoles"][0]["knowledge_roles"] = Json::array();
		rejects(
		    [&]
		    {
			    load(forbidden_message);
		    },
		    "Controller cannot bind inbox payload forbidden to owning Console");
		auto public_source = ai_messages_source;
		public_source["knowledge"]["schemas"][0]["ship_publishable"] = true;
		public_source["knowledge"]["schemas"][0]["fields"][0]["publishable"] = true;
		public_source["entities"][0]["stations"][0]["consoles"].push_back({{"id", "spectator"},
		                                                                   {"definition", "test.console"},
		                                                                   {"systems", Json::array()},
		                                                                   {"knowledge_roles", Json::array()}});
		public_source["messages"][0]["urgency"] = 2;
		public_source["messages"][0]["automatic"] = true;
		const auto public_content = load(public_source);
		auto claim_public = [&](World &run)
		{
			require(run.claim(101, 1, "drive") && run.claim(102, 1, "observer") && run.claim(103, 1, "spectator"),
			        "public messaging claims");
		};
		World public_messages;
		public_messages.load(public_content, "test.map");
		claim_public(public_messages);
		require(public_messages.submit(101, {1, "preset", 1, {}}) && public_messages.submit(102, {1, "preset", 1, {}}),
		        "important message sender and recipient use AI");
		public_messages.tick();
		World public_resume;
		public_resume.restore(public_content, public_messages.save());
		for (auto *run : {&public_messages, &public_resume})
			for (int tick = 0; tick < 6; ++tick)
				run->tick();
		const auto public_view = public_messages.observe(103)->gameplay;
		const auto target_view = public_messages.observe(101)->gameplay;
		const auto evidence_count = [](const Json &board)
		{
			return std::count_if(board.at("shared").begin(), board.at("shared").end(),
			                     [](const auto &record)
			                     {
				                     return record.at("schema") != "phoenix.camera";
			                     });
		};
		require(public_view.at("messages").size() == 1 && evidence_count(public_view) == 1,
		        "important AI-to-AI message reaches spectator history and shared entity board");
		require(public_view.at("messages")[0].at("id") == target_view.at("messages")[0].at("id") &&
		            public_view.at("messages")[0].at("public") == true &&
		            public_view.at("messages")[0].at("offer_only") == true &&
		            public_view.at("messages")[0].at("actionable") == false &&
		            target_view.at("messages")[0].at("actionable") == true,
		        "public copy preserves identity without broadening Controller actionability");
		require(public_messages.save() == public_resume.save(), "pending public delivery restores exactly");
		const auto board = public_messages.public_board(1);
		require(board.at("messages").size() == 1 && evidence_count(board) == 1 && !board.contains("controllers") &&
		            !board.contains("records") && !board.contains("controls"),
		        "public board deduplicates Console copies without exposing private or command sections");
		require(board.at("messages")[0].at("actionable") == false && board.at("messages")[0].at("offer_only") == true,
		        "public board always presents read-only message history");
		public_resume.clear_claims();
		require(!public_resume.observe(103) && public_resume.public_board(1) == board,
		        "public board is available without a Console claim");
		const auto board_rows = public_board_readouts(board, public_messages.ticks());
		require(!board_rows.empty() && std::all_of(board_rows.begin(), board_rows.end(),
		                                           [](const auto &row)
		                                           {
			                                           return row.first.starts_with("viewscreen.readout/");
		                                           }),
		        "Viewscreen exposes dedicated read-only semantic identities");
		const auto board_messages = console_messages(board, true);
		require(board_messages.size() == 1 && !board_messages[0].sender.empty() &&
		            board_messages[0].accessible_text().find("sender_system") == std::string::npos,
		        "Viewscreen messages have a named speaker and prose-only semantics");

		auto crowded_board = board;
		crowded_board["shared"] = Json::array();
		crowded_board["records"] =
		    Json::array({{{"schema", "private-secret"}, {"fields", {{"secret", "DO-NOT-DISPLAY"}}}}});
		crowded_board["messages"] = Json::array();
		for (int i = 0; i < 10; ++i)
		{
			auto row = board.at("messages")[0];
			row["id"] = i + 1;
			row["sequence"] = i + 1;
			crowded_board["messages"].push_back(row);
		}
		const auto crowded_rows = public_board_readouts(crowded_board, 100);
		const auto crowded_messages = console_messages(crowded_board, true);
		require(crowded_rows.empty() && crowded_messages.size() == 10 &&
		            std::none_of(crowded_messages.begin(), crowded_messages.end(),
		                         [](const auto &row)
		                         {
			                         return row.text.find("DO-NOT-DISPLAY") != std::string::npos;
		                         }),
		        "scrollable public messages exclude Console-private records without an eight-row cut-off");

		auto public_session_source = public_source;
		public_session_source["entities"][0]["control_layout"]["default_profile"] = "automatic";
		public_session_source["entities"][0]["systems"]["steering"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}};
		public_session_source["maps"] = controller_session_fixture()["maps"];
		auto public_host = make_host_world();
		public_host->add_pack(archive(public_session_source));
		public_host->start_server({"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "drive"});
		for (int tick = 0; tick < 7; ++tick)
			public_host->tick();
		require(!public_host->observation() && public_host->public_board().at("messages").size() == 1,
		        "unclaimed host SessionPort forwards the public board for its focus entity");
		World published_resume;
		published_resume.restore(public_content, public_messages.save());
		auto persistent_public_view = public_view;
		persistent_public_view["notifications"] = Json::array();
		require(published_resume.observe(103)->gameplay == persistent_public_view,
		        "delivered public history survives restore without replaying transient human notifications");
		auto forged_public_save = public_messages.save();
		const auto spectator_id = forged_public_save.at("knowledge").at("ships")[0].at("consoles").at("spectator");
		for (auto &delivery : forged_public_save["knowledge"]["runtime"]["deliveries"])
			if (delivery.at("console") == spectator_id)
				delivery["offer_only"] = false;
		rejects(
		    [&]
		    {
			    published_resume.restore(public_content, forged_public_save);
		    },
		    "restore cannot upgrade public spectator copy to addressed actionability");
		World human_target;
		human_target.load(public_content, "test.map");
		claim_public(human_target);
		require(human_target.submit(101, {1, "preset", 1, {}}) && human_target.submit(102, {1, "preset", 1, {}}),
		        "important message initially targets AI");
		human_target.tick();
		require(human_target.submit(101, {1, "preset", 0, {}}), "recipient becomes human before delayed delivery");
		for (int tick = 0; tick < 6; ++tick)
			human_target.tick();
		require(human_target.observe(101)->gameplay.at("messages").size() == 1 &&
		            human_target.observe(103)->gameplay.at("messages").empty() &&
		            evidence_count(human_target.observe(103)->gameplay) == 0,
		        "current human recipient suppresses automatic AI-to-AI public copy");
		auto shared_reader_source = public_source;
		auto &shared_entity = shared_reader_source["entities"][0];
		shared_entity["systems"]["aux"] = {{"kind", "engine"}, {"max_speed_mm_s", 17000}, {"acceleration_mm_s2", 1000}};
		shared_entity["stations"][0]["consoles"][2]["feeds"] = {"aux"};
		shared_entity["control_layout"]["controllers"].push_back(
		    {{"id", "reader"},
		     {"console", "spectator"},
		     {"tree", "test.shared"},
		     {"commands", Json::array({{{"id", "speed"}, {"action", "speed_mm_s"}, {"consumers", {"aux"}}}})}});
		shared_entity["control_layout"]["profiles"][0]["consoles"]["spectator"] = {{"human", {"reader"}}};
		shared_entity["control_layout"]["profiles"][1]["consoles"]["spectator"] = {{"ai", {"reader"}}};
		shared_reader_source["trees"].push_back(
		    Json::parse(R"json({"id":"test.shared","context":"controller","initial":"run",
		 "command_bindings":[{"channel":"speed"}],
		 "record_bindings":[{"name":"shared_amount","scope":"ship","schema":"request","field":"amount","subject":"self"}],
		 "states":[{"id":"run","running":[{"action":"write.speed","arguments":[{"observation":"shared_amount"}]}]}]})json"));
		shared_reader_source["maps"][0]["entities"].push_back(
		    {{"id", 2}, {"definition", "test.controllable"}, {"x_mm", 1000000}, {"y_mm", 0}, {"heading_mdeg", 0}});
		World shared_reader;
		// Mix categories in one tree so changes to the compiled layout cannot
		// silently redirect a record read into telemetry, commands or inbox data.
		{
			auto mixed = shared_reader_source;
			auto &tree = mixed["trees"].back();
			tree["system_bindings"] = Json::parse(R"json([
			 {"name":"actual_request","system":"aux","field":"requested"}])json");
			tree["command_reads"] = Json::parse(R"json([
			 {"name":"prior_speed","controller":"reader","channel":"speed","type":"integer"}])json");
			tree["states"][0]["transitions"] = Json::parse(R"json([
			 {"target":"verified","conditions":[
			 {"left":{"observation":"shared_amount.available"},"op":"eq","right":true},
			 {"left":{"observation":"shared_amount"},"op":"eq","right":6000},
			 {"left":{"observation":"actual_request"},"op":"eq","right":6000},
			 {"left":{"observation":"prior_speed"},"op":"eq","right":6000},
			 {"left":{"observation":"command.speed.accepted"},"op":"eq","right":true},
			 {"left":{"observation":"message.available"},"op":"eq","right":false},
			 {"left":{"observation":"result.available"},"op":"eq","right":true},
			 {"left":{"observation":"result.success"},"op":"eq","right":true}]}])json");
			tree["states"].push_back(Json::parse(R"json(
			 {"id":"verified","entry":[{"action":"write.speed","arguments":[4321]}]})json"));
			const auto content = load(mixed);
			World run;
			run.load(content, "test.map");
			claim_public(run);
			for (const auto claimant : {101, 102, 103})
				require(run.submit(claimant, {1, "preset", 1, {}}), "mixed binding AI profile");
			for (int tick = 0; tick < 4; ++tick)
				run.tick();
			World resumed;
			resumed.restore(content, run.save());
			for (int tick = 0; tick < 8; ++tick)
			{
				run.tick();
				resumed.tick();
			}
			require(run.observe(103)->systems[0].requested == 4321,
			        "mixed Controller bindings preserve values and metadata through actual tree decisions");
			require(run.save() == resumed.save(),
			        "mixed Controller bindings rebuild on restore without changing continuation");
		}
		shared_reader.load(load(shared_reader_source), "test.map");
		claim_public(shared_reader);
		require(shared_reader.claim(104, 2, "spectator"), "other entity public reader claim");
		for (const auto claimant : {101, 102, 103})
			require(shared_reader.submit(claimant, {1, "preset", 1, {}}), "shared reader AI profile");
		for (int tick = 0; tick < 8; ++tick)
			shared_reader.tick();
		const auto shared_reader_view = shared_reader.observe(103);
		require(shared_reader_view->systems[0].requested == 6000,
		        "Controller with no private reader role consumes same-entity published evidence");
		const auto other_entity_view = shared_reader.observe(104);
		for (const auto &record : other_entity_view->gameplay.at("shared"))
			require(record.at("subject") != 1, "public record visibility never crosses entity boundary");
		auto evidence_source = shared_reader_source;
		auto latest_source = shared_reader_source;
		latest_source["maps"][0]["entities"].erase(1);
		latest_source["entities"][0]["systems"]["port"]["records"] = Json::parse(R"json([
		 {"schema":"request","producer":"crew","subject":77,"fields":{"amount":"requested"}}])json");
		latest_source["entities"][0]["publications"] = Json::parse(R"json([
		 {"id":"objective","source_console":"drive","schema":"request","subject":77,"fields":["amount"],"live":false}])json");
		latest_source["trees"].back()["record_bindings"][0]["subject"] = "latest";
		latest_source["trees"].back()["states"][0]["running"][0]["arguments"] =
		    Json::array({{{"observation", "shared_amount.subject"}}});
		latest_source["trees"].back()["states"][0]["transitions"] = Json::parse(R"json([
		 {"target":"gone","conditions":[{"left":{"observation":"shared_amount.withdrawn"},"op":"eq","right":true}]}])json");
		latest_source["trees"].back()["states"].push_back(Json::parse(R"json(
		 {"id":"gone","running":[{"action":"write.speed","arguments":[88]}]})json"));
		World latest_world;
		const auto latest_catalog = load(latest_source);
		latest_world.load(latest_catalog, "test.map");
		claim_public(latest_world);
		for (const auto claimant : {101, 102, 103})
			latest_world.submit(claimant, {1, "preset", 1, {}});
		for (int tick = 0; tick < 8; ++tick)
			latest_world.tick();
		require(latest_world.submit(101, {1, "publish.objective", 0, {}}), "publish objective-like non-entity subject");
		latest_world.tick();
		latest_world.tick();
		require(latest_world.observe(103)->systems[0].requested == 77,
		        "latest shared binding exposes permitted objective subject instead of assuming entity identity");
		auto latest_tombstone = latest_world.save();
		for (auto &record : latest_tombstone["knowledge"]["runtime"]["records"])
			if (record["value"]["subject"] == 77)
			{
				record["value"]["withdrawn"] = true;
				record["value"]["fields"] = Json::array();
			}
		World latest_withdrawn;
		latest_withdrawn.restore(latest_catalog, latest_tombstone);
		latest_withdrawn.tick();
		latest_withdrawn.tick();
		require(latest_withdrawn.observe(103)->systems[0].requested == 88,
		        "latest subject tombstone cannot resurrect an older subject's record");
		auto private_latest = latest_source;
		private_latest["trees"].back()["record_bindings"][0]["scope"] = "console";
		rejects(
		    [&]
		    {
			    load(private_latest);
		    },
		    "latest subject cannot bypass private Console reader roles");
		evidence_source["maps"][0]["entities"].erase(1);
		evidence_source["knowledge"]["schemas"][0]["stale_after_ticks"] = 10;
		evidence_source["trees"].back()["states"] = Json::parse(R"json([
		 {"id":"run","running":[{"action":"write.speed","arguments":[{"observation":"shared_amount"}]}],
		  "transitions":[
		   {"target":"withdrawn","conditions":[{"left":{"observation":"shared_amount.withdrawn"},"op":"eq","right":true}]},
		   {"target":"stale","conditions":[{"left":{"observation":"shared_amount.stale"},"op":"eq","right":true}]}]},
		 {"id":"withdrawn","running":[{"action":"write.speed","arguments":[300]}]},
		 {"id":"stale","running":[{"action":"write.speed","arguments":[200]}]}
		])json");
		const auto evidence_catalog = load(evidence_source);
		World evidence_world;
		evidence_world.load(evidence_catalog, "test.map");
		claim_public(evidence_world);
		for (const auto claimant : {101, 102, 103})
			require(evidence_world.submit(claimant, {1, "preset", 1, {}}), "evidence reader AI profile");
		for (int tick = 0; tick < 8; ++tick)
			evidence_world.tick();
		require(evidence_world.observe(103)->systems[0].requested == 6000,
		        "fresh permitted record supplies Controller value");
		const auto fresh_evidence = evidence_world.save();
		World evidence_resumed;
		evidence_resumed.restore(evidence_catalog, fresh_evidence);
		for (int tick = 0; tick < 10; ++tick)
		{
			evidence_world.tick();
			evidence_resumed.tick();
		}
		require(evidence_world.observe(103)->systems[0].requested == 200 &&
		            evidence_world.save() == evidence_resumed.save(),
		        "stale metadata drives Controller fallback with exact restored continuation");
		auto withdrawn_evidence = fresh_evidence;
		for (auto &record : withdrawn_evidence["knowledge"]["runtime"]["records"])
			if (record["value"]["schema"] == evidence_catalog.knowledge_definition.schema("request"))
			{
				record["value"]["withdrawn"] = true;
				record["value"]["fields"] = Json::array();
			}
		World withdrawn_reader;
		withdrawn_reader.restore(evidence_catalog, withdrawn_evidence);
		withdrawn_reader.tick();
		withdrawn_reader.tick();
		require(withdrawn_reader.observe(103)->systems[0].requested == 300,
		        "restored withdrawn stream exposes tombstone metadata without resurrecting its value");
		shared_reader_source["trees"].back()["record_bindings"][0]["scope"] = "console";
		rejects(
		    [&]
		    {
			    load(shared_reader_source);
		    },
		    "shared readability grants no Console-private reader authority");
		auto ordinary_source = public_source;
		ordinary_source["messages"][0]["urgency"] = 1;
		World ordinary_messages;
		ordinary_messages.load(load(ordinary_source), "test.map");
		claim_public(ordinary_messages);
		require(ordinary_messages.submit(101, {1, "preset", 1, {}}) &&
		            ordinary_messages.submit(102, {1, "preset", 1, {}}),
		        "ordinary AI message operators");
		for (int tick = 0; tick < 6; ++tick)
			ordinary_messages.tick();
		require(ordinary_messages.observe(103)->gameplay.at("messages").empty(),
		        "ordinary AI coordination stays targeted");
		require(ordinary_messages.public_board(1).at("messages").empty(),
		        "targeted private coordination never leaks through the public API");
		auto broadcast_source = public_source;
		broadcast_source["messages"][0]["destination"] = {{"kind", "ship"}};
		broadcast_source["messages"][0]["urgency"] = 0;
		World broadcast;
		broadcast.load(load(broadcast_source), "test.map");
		claim_public(broadcast);
		send.generation = controller(broadcast, 102, "port").at("generation").get<std::uint64_t>();
		require(broadcast.submit(102, send), "human Controller sends entity-wide message");
		for (int tick = 0; tick < 6; ++tick)
			broadcast.tick();
		require(broadcast.observe(103)->gameplay.at("messages").size() == 1 &&
		            evidence_count(broadcast.observe(103)->gameplay) == 1,
		        "human entity-wide message publishes regardless urgency");
		auto private_publication = public_source;
		private_publication["knowledge"]["schemas"][0]["ship_publishable"] = false;
		rejects(
		    [&]
		    {
			    load(private_publication);
		    },
		    "important message cannot publish private schema");
		private_publication = public_source;
		private_publication["knowledge"]["schemas"][0]["fields"][0]["publishable"] = false;
		rejects(
		    [&]
		    {
			    load(private_publication);
		    },
		    "important message cannot publish private field");
		private_publication = public_source;
		private_publication["messages"][0]["parameters"]["secret"] = {{"argument", "detail"}};
		rejects(
		    [&]
		    {
			    load(private_publication);
		    },
		    "public presentation cannot expose argument absent from public payload");
		auto reporter_source = ai_messages_source;
		auto &reporter_entity = reporter_source["entities"][0];
		reporter_entity["control_layout"]["controllers"][0].erase("tree");
		reporter_entity["control_layout"]["controllers"][1]["tree"] = "test.receiver";
		reporter_source["trees"].erase(0);
		reporter_source["messages"][0]["destination"]["console"] = "observer";
		reporter_source["messages"][0]["delay_ticks"] = 1;
		auto recovery_template = reporter_source["messages"][0];
		recovery_template["id"] = "test.recovered";
		reporter_source["messages"].push_back(recovery_template);
		reporter_entity["message_templates"].push_back("test.recovered");
		reporter_entity["systems"]["port"]["reporters"] = Json::parse(R"json([
		 {"id":"high","field":"requested","above":4000,"message":"test.request","recovery_message":"test.recovered",
		  "debounce_ticks":2,"recovery_ticks":2,"minimum_gap_ticks":5}])json");
		const auto reporter_content = load(reporter_source);
		auto initially_hot_source = reporter_source;
		initially_hot_source["entities"][0]["control_layout"]["controllers"][0]["commands"][0]["initial"] = 6000;
		World initially_hot;
		initially_hot.load(load(initially_hot_source), "test.map");
		require(initially_hot.claim(101, 1, "drive") && initially_hot.claim(102, 1, "observer"), "initial hot claims");
		initially_hot.tick();
		initially_hot.tick();
		require(initially_hot.observe(102)->gameplay.at("messages").empty(), "first hot sample also obeys debounce");
		for (int tick = 0; tick < 3; ++tick)
			initially_hot.tick();
		require(initially_hot.observe(102)->gameplay.at("messages").size() == 1,
		        "initial sustained warning eventually reports");
		World reporting;
		reporting.load(reporter_content, "test.map");
		require(reporting.claim(101, 1, "drive") && reporting.claim(102, 1, "observer"), "reporter claims");
		require(reporting.submit(102, {1, "preset", 1, {}}), "report recipient delegates to AI");
		reporting.tick();
		require(reporting.observe(102)->gameplay.at("messages").empty(), "initial healthy telemetry is not recovery");
		require(reporting.submit(101, speed(reporting, 101, "port", 6000)), "human controls reporting physical System");
		reporting.tick();
		reporting.tick();
		require(reporting.observe(102)->gameplay.at("messages").empty(), "warning waits for configured debounce");
		for (int tick = 0; tick < 4; ++tick)
			reporting.tick();
		const auto warning_history = reporting.observe(102)->gameplay.at("messages");
		require(warning_history.size() == 1 && requested(reporting, "starboard") == 6000,
		        "always-active physical report reaches AI while producer remains human controlled");
		require(warning_history[0].at("payload").at("fields").at("amount") == 6000,
		        "report carries measured physical telemetry");
		require(warning_history[0].at("payload").at("kind") == static_cast<unsigned>(EvidenceKind::Fact) &&
		            warning_history[0].at("origin") == static_cast<unsigned>(ControlOrigin::Reporter),
		        "physical report carries fact provenance distinct from Controller intent");
		World reporting_resume;
		reporting_resume.restore(reporter_content, reporting.save());
		for (auto *run : {&reporting, &reporting_resume})
		{
			require(run->submit(101, {1, "preset", 1, {}}), "reporter producer hands over");
			for (int tick = 0; tick < 3; ++tick)
				run->tick();
			require(run->observe(102)->gameplay.at("messages").size() == 1,
			        "unchanged warning is not repeated after restore/handover");
			require(run->submit(101, {1, "preset", 0, {}}), "producer returns human");
			run->tick();
			require(run->submit(101, speed(*run, 101, "port", 0)), "physical source recovers");
			run->tick();
			run->tick();
			require(run->observe(102)->gameplay.at("messages").size() == 1, "recovery waits for hysteresis");
			for (int tick = 0; tick < 3; ++tick)
				run->tick();
			require(run->observe(102)->gameplay.at("messages").size() == 2, "stable recovery emits once");
		}
		require(reporting.save() == reporting_resume.save(), "reporter latch continuation survives restore");
		auto altered_reporter_save = reporting.save();
		altered_reporter_save["knowledge"].erase("reporter_contract");
		rejects(
		    [&]
		    {
			    reporting_resume.restore(reporter_content, altered_reporter_save);
		    },
		    "restore refuses missing reporter contract");
		require(reporting.save() == reporting_resume.save(), "failed reporter restore preserves live state");
		auto gap_source = reporter_source;
		gap_source["entities"][0]["systems"]["port"]["reporters"][0]["minimum_gap_ticks"] = 20;
		World gap_reports;
		gap_reports.load(load(gap_source), "test.map");
		require(gap_reports.claim(101, 1, "drive") && gap_reports.claim(102, 1, "observer"), "minimum-gap claims");
		gap_reports.tick();
		require(gap_reports.submit(101, speed(gap_reports, 101, "port", 6000)), "minimum-gap warning input");
		for (int tick = 0; tick < 5; ++tick)
			gap_reports.tick();
		require(gap_reports.observe(102)->gameplay.at("messages").size() == 1,
		        "first warning is not delayed by minimum gap");
		require(gap_reports.submit(101, speed(gap_reports, 101, "port", 0)), "minimum-gap recovery input");
		for (int tick = 0; tick < 5; ++tick)
			gap_reports.tick();
		require(gap_reports.observe(102)->gameplay.at("messages").size() == 1,
		        "minimum gap delays otherwise stable recovery");
		for (int tick = 0; tick < 16; ++tick)
			gap_reports.tick();
		require(gap_reports.observe(102)->gameplay.at("messages").size() == 2,
		        "recovery delivers when minimum gap expires");
		auto invalid_reporter = reporter_source;
		invalid_reporter["entities"][0]["systems"]["port"]["reporters"][0]["field"] = "heat";
		rejects(
		    [&]
		    {
			    load(invalid_reporter);
		    },
		    "Catalog refuses reporter telemetry absent from fitted physical System");
		invalid_reporter = reporter_source;
		invalid_reporter["entities"][0]["systems"]["port"]["reporters"][0]["kind"] = "claim";
		rejects(
		    [&]
		    {
			    load(invalid_reporter);
		    },
		    "physical reporter cannot originate editorial claims");
		messages_source["messages"][0]["destination"]["id"] = "starboard";
		rejects(
		    [&]
		    {
			    load(messages_source);
		    },
		    "physical System cannot masquerade as a Controller recipient");

		auto widget_source = controller_session_fixture();
		widget_source["entities"][0]["stations"][0]["consoles"].erase(1);
		widget_source["entities"][0]["control_layout"]["controllers"][0]["commands"].push_back(
		    {{"id", "heading"}, {"action", "heading_mdeg"}, {"consumers", {"steering"}}});
		widget_source["consoles"][0]["widgets"] = Json::parse(R"json([
		 {"id":"shared","kind":"throttle","label":"Shared throttle","controller":"propulsion","channel":"speed",
		  "x":0,"y":0,"width":500,"height":500},
		 {"id":"course","kind":"heading","label":"Course","controller":"propulsion","channel":"heading",
		  "x":500,"y":0,"width":500,"height":500}])json");
		const auto widget_catalog = load(widget_source);
		const auto widget_public = resolve_packs({console_assets(widget_catalog, "test.controllable")});
		require(widget_public.consoles.at("test.console").at("widgets")[0].at("controller") == "propulsion" &&
		            widget_public.consoles.at("test.console").at("widgets")[0].at("channel") == "speed",
		        "client-safe console assets preserve authored Controller widget addresses");
		for (const auto &invalid_kind : {"foreign", "mismatch", "missing", "ambiguous"})
		{
			auto invalid_widget = widget_source;
			auto &widget = invalid_widget["consoles"][0]["widgets"][0];
			if (std::string(invalid_kind) == "foreign")
				widget["controller"] = "absent";
			if (std::string(invalid_kind) == "mismatch")
				widget["channel"] = "heading";
			if (std::string(invalid_kind) == "missing")
				widget.erase("channel");
			if (std::string(invalid_kind) == "ambiguous")
				widget["system"] = "port";
			rejects(
			    [&]
			    {
				    load(invalid_widget);
			    },
			    "Catalog rejects invalid Controller widget address/action");
		}
		auto widget_session = make_local_session();
		widget_session->add_pack(archive(widget_source));
		widget_session->start({"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "drive"});
		ConsoleModel widget_model;
		widget_model.refresh(widget_session);
		const auto throttle_binding = widget_model.widget_system("shared");
		const auto heading_binding = widget_model.widget_system("course");
		require(!widget_model.command("speed_mm_s", 17001, throttle_binding) &&
		            !widget_model.command("heading_mdeg", 1000, throttle_binding) &&
		            !widget_model.command("speed_mm_s", 1000, "controller/absent/speed"),
		        "numeric widget boundary refuses range, action and foreign channel mismatches");
		require(widget_model.system_maximum(throttle_binding) == 17000 &&
		            widget_model.set_speed_fraction(0.5, throttle_binding) &&
		            widget_model.set_heading_degrees(90, heading_binding),
		        "authored widgets submit typed Controller commands through local session");
		widget_session->tick();
		widget_model.refresh(widget_session);
		require(widget_model.system_request(throttle_binding) == 8500 &&
		            widget_model.system_request(heading_binding) == 90000,
		        "widget readouts use separate accepted command sections");
		const auto widget_observation = widget_session->observation();
		for (const auto &system : widget_observation->systems)
			if (system.id == "port" || system.id == "starboard")
				require(system.requested == 8500, "shared authored throttle reaches both physical engines");
		for (const auto &[id, label] : widget_model.gameplay_controls())
			if (label == "automatic")
				require(widget_model.act(id), "widget Console switches complexity");
		widget_session->tick();
		widget_model.refresh(widget_session);
		require(!widget_model.set_speed_fraction(0.25, throttle_binding), "AI-owned widget refuses human command");
		auto split_widget_source = widget_source;
		auto &split_widget_commands =
		    split_widget_source["entities"][0]["control_layout"]["controllers"][0]["commands"];
		split_widget_commands[0]["consumers"] = {"port"};
		auto second_throttle = split_widget_commands[0];
		second_throttle["id"] = "second_speed";
		second_throttle["consumers"] = {"starboard"};
		split_widget_commands.push_back(second_throttle);
		auto second_widget = split_widget_source["consoles"][0]["widgets"][0];
		second_widget["id"] = "second";
		second_widget["channel"] = "second_speed";
		second_widget["y"] = 500;
		split_widget_source["consoles"][0]["widgets"].push_back(second_widget);
		auto split_widget_session = make_local_session();
		split_widget_session->add_pack(archive(split_widget_source));
		split_widget_session->start({"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "drive"});
		ConsoleModel split_widget_model;
		split_widget_model.refresh(split_widget_session);
		require(split_widget_model.set_speed_fraction(0.25, split_widget_model.widget_system("shared")) &&
		            split_widget_model.set_speed_fraction(0.75, split_widget_model.widget_system("second")),
		        "two authored throttles address independent command channels");
		split_widget_session->tick();
		split_widget_model.refresh(split_widget_session);
		require(split_widget_model.system_request(split_widget_model.widget_system("shared")) == 4250 &&
		            split_widget_model.system_request(split_widget_model.widget_system("second")) == 12750,
		        "independent widget submissions cannot overwrite each other's settings");

		const auto session_archive = archive(controller_session_fixture());
		const GameSetup setup{"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "drive"};
		auto local = make_local_session();
		local->add_pack(session_archive);
		local->start(setup);
		ConsoleModel model;
		model.refresh(local);
		std::string increase;
		for (const auto &[id, label] : model.gameplay_controls())
			if (label == "propulsion speed 850")
				increase = id;
		require(!increase.empty() && model.act(increase),
		        "ConsoleModel activates a Controller descriptor through SessionPort");
		local->tick();
		require(local->observation()->gameplay.at("controllers")[0].at("commands").at("speed").at("value") == 850,
		        "presentation command retains Controller/channel/generation through local execution");
		auto recovered = make_local_session();
		recovered->restore(local->save());
		require(recovered->recovery_pending() && !recovered->observation(),
		        "portable Controller recovery drops live authority");
		recovered->resume_local();
		require(recovered->observation()->gameplay.at("controllers")[0].at("commands").at("speed").at("value") == 850,
		        "portable recovery retains committed Controller settings");
		model.refresh(recovered);
		bool profile_changed = false;
		for (const auto &[id, label] : model.gameplay_controls())
			if (label == "automatic")
				profile_changed = model.act(id);
		require(profile_changed, "Console settings expose profile selection through the presentation model");
		recovered->tick();
		require(!recovered->observation()->gameplay.at("controllers")[0].at("human").get<bool>(),
		        "Console profile action changes effective Controller operator");

		auto host = make_host_world();
		host->add_pack(session_archive);
		host->start_server(setup);
		GameExecution execution(host, {{"server", "test.slot"}});
		const auto ship_id = *host->authoritative_world().ship_for_slot("test.slot");
		const std::string lease(64, 'a');
		require(execution.apply("server", {{"kind", "station-claim"},
		                                   {"owner", "crew"},
		                                   {"route", 1},
		                                   {"ship", ship_id},
		                                   {"seat", "drive"},
		                                   {"lease", lease}}),
		        "committed execution grants an authenticated Console lease");
		const auto projected = execution.observe("server", "crew");
		OperatingCommand framed{ship_id, {}, 4200, {}};
		framed.controller = "propulsion";
		framed.channel = "speed";
		framed.generation = projected->gameplay.at("controllers")[0].at("generation").get<std::uint64_t>();
		auto frame = encode_operating_command(framed);
		frame.update({{"kind", "command"}, {"owner", "crew"}, {"route", 1}, {"lease", lease}, {"sequence", 1}});
		require(execution.apply("server", frame) && !execution.apply("server", frame),
		        "committed command frames preserve Controller metadata and reject sequence replay");
		require(execution.apply("server", {{"kind", "tick"}, {"tick", 1}}), "committed tick executes routed command");
		require(
		    execution.observe("server", "crew")->gameplay.at("controllers")[0].at("commands").at("speed").at("value") ==
		        4200,
		    "frame execution reaches the shared command section");
		execution.absent("server");
		frame["sequence"] = 2;
		require(!execution.apply("server", frame), "server absence cannot revive a stale Controller lease");
		std::cout << "PASS Controller sections, physical subscriptions, operator generations and continuation\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
