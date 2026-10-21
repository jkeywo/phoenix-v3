#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <iostream>
import phoenix.simulation;
#include "../../src/network/projection.hpp"
#include "../../src/simulation/effect_receipts.hpp"
using namespace phoenix;
using namespace phoenix::test;
Json fixture()
{
	auto source = controller_fixture();
	auto &s = source["entities"][0];
	s["id"] = "test.away";
	s["systems"] = {{"radio",
	                 {{"kind", "comms"},
	                  {"range_mm", 100000},
	                  {"energy_per_second", 20},
	                  {"power_store", "cell"},
	                  {"compartment", "bridge"}}},
	                {"transport",
	                 {{"kind", "displacement"},
	                  {"range_mm", 20000},
	                  {"capacity", 4},
	                  {"transfer_ticks", 3},
	                  {"energy_per_second", 20},
	                  {"power_store", "cell"},
	                  {"compartment", "bridge"}}},
	                {"dispatch", {{"kind", "dispatch"}, {"compartment", "bridge"}}},
	                {"team", {{"kind", "repair_team"}, {"hp_per_second", 20}, {"compartment", "workshop"}}}};
	s["systems"]["engine"] = {
	    {"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}, {"compartment", "bridge"}};
	s["systems"]["steering"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}, {"compartment", "bridge"}};
	s["resources"] = {{"stores", Json::array({{{"id", "cell"},
	                                           {"capacity", 100000},
	                                           {"initial", 100000},
	                                           {"charge_rate", 10000},
	                                           {"discharge_rate", 10000}}})}};
	s["combat"] = {{"repair_console", "observer"},
	               {"decks", Json::array({{{"id", "deck"},
	                                       {"weight", 1},
	                                       {"compartments", Json::array({{{"id", "bridge"},
	                                                                      {"x_mm", 1000},
	                                                                      {"y_mm", 0},
	                                                                      {"half_width_mm", 1000},
	                                                                      {"half_length_mm", 1000},
	                                                                      {"hp", 100},
	                                                                      {"resistance_per_m", 1},
	                                                                      {"recovery_hp", 10}},
	                                                                     {{"id", "workshop"},
	                                                                      {"x_mm", -1000},
	                                                                      {"y_mm", 0},
	                                                                      {"half_width_mm", 1000},
	                                                                      {"half_length_mm", 1000},
	                                                                      {"hp", 100},
	                                                                      {"resistance_per_m", 1},
	                                                                      {"recovery_hp", 10}}})}}})},
	               {"connections", Json::array({{{"from", "bridge"}, {"to", "workshop"}, {"travel_ticks", 3}}})}};
	s["combat"]["destruction"] = {{"visual", "test.away"},
	                              {"sound", "test-core:assets/pixel.png"},
	                              {"growth_ticks", 5},
	                              {"fade_ticks", 10},
	                              {"size_mm", 1000}};
	s["stations"] = Json::array(
	    {{{"id", "helm"},
	      {"consoles", Json::array({{{"id", "drive"},
	                                 {"definition", "test.console"},
	                                 {"systems", Json::array()},
	                                 {"feeds", {"radio", "transport", "dispatch", "team", "engine", "steering"}}}})}},
	     {{"id", "engineering"},
	      {"consoles", Json::array({{{"id", "observer"},
	                                 {"definition", "test.console"},
	                                 {"systems", Json::array()},
	                                 {"feeds", {"team"}}}})}}});
	Json controllers = Json::array();
	for (const auto &key : {"radio", "transport", "dispatch", "team", "engine", "steering"})
		controllers.push_back(
		    {{"id", key},
		     {"console", std::string(key) == "team" ? "observer" : "drive"},
		     {"fallback", "hold"},
		     {"commands",
		      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {key}}}})}});
	s["control_layout"] = {
	    {"schema", 1},
	    {"controllers", controllers},
	    {"default_profile", "direct"},
	    {"profiles", Json::array({{{"id", "direct"},
	                               {"consoles",
	                                {{"drive", {{"human", {"radio", "transport", "dispatch", "engine", "steering"}}}},
	                                 {"observer", {{"human", {"team"}}}}}}}})}};
	s["personnel"] = {
	    {"departments", Json::array({{{"id", "navigation"}, {"label", "Navigation"}, {"station", "helm"}},
	                                 {{"id", "engineering"}, {"label", "Engineering"}, {"station", "engineering"}}})},
	    {"traits", Json::array({{{"id", "navigator"}, {"label", "Navigator"}, {"bonuses", Json::object()}},
	                            {{"id", "repair"}, {"label", "Engineer"}, {"bonuses", {{"repair", 250}}}}})},
	    {"duties", Json::array()},
	    {"officers", Json::array({{{"id", "nav"},
	                               {"name", "Navigator"},
	                               {"department", "navigation"},
	                               {"compartment", "bridge"},
	                               {"duty", "transport"},
	                               {"traits", {"navigator"}}},
	                              {{"id", "chief"},
	                               {"name", "Chief"},
	                               {"department", "engineering"},
	                               {"compartment", "workshop"},
	                               {"duty", "team-duty"},
	                               {"traits", {"repair"}}}})}};
	for (const auto &key : {"radio", "transport", "dispatch", "team", "engine", "steering"})
		s["personnel"]["duties"].push_back({{"id", key},
		                                    {"label", key},
		                                    {"controller", key},
		                                    {"console", std::string(key) == "team" ? "observer" : "drive"},
		                                    {"department", std::string(key) == "team" ? "engineering" : "navigation"},
		                                    {"compartment", "bridge"},
		                                    {"effects", Json::object()}});
	s["personnel"]["duties"][1]["required_trait"] = "navigator";
	s["personnel"]["duties"].push_back({{"id", "team-duty"},
	                                    {"label", "Team"},
	                                    {"team", "team"},
	                                    {"department", "engineering"},
	                                    {"compartment", "workshop"},
	                                    {"effects", {{"team", {"repair"}}}}});
	s["comms"] = {{"characters", Json::array({{{"id", "leader"}, {"name_key", "Remote leader"}, {"public", true}}})}};
	source["trees"].push_back(Json::parse(
	    R"({"id":"test.dialogue","context":"conversation","initial":"start","lines":{"intro":{"speaker":"team","text_key":"Mission available"},"done":{"speaker":"team","text_key":"Work complete"}},"prompts":{"choice":{"speaker":"team","text_key":"Proceed?","timeout_ticks":30,"default_choice":"yes","choices":[{"id":"yes","text_key":"Proceed"}]}},"states":[{"id":"start","entry":[{"action":"say","arguments":["intro"]}],"transitions":[{"target":"ask","conditions":[{"left":{"observation":"event"},"op":"eq","right":"arrived"}]}]},{"id":"ask","entry":[{"action":"offer","arguments":["choice"]}],"transitions":[{"target":"done","conditions":[{"left":{"observation":"event"},"op":"eq","right":"done"}]}]},{"id":"done","entry":[{"action":"say","arguments":["done"]}]}]})"));
	source["trees"].push_back(Json::parse(
	    R"({"id":"test.away-tree","context":"away","initial":"wait","states":[{"id":"wait","transitions":[{"target":"work","conditions":[{"left":{"observation":"deployed"},"op":"eq","right":true}]}]},{"id":"work","entry":[{"action":"checkpoint","arguments":["repair"]},{"action":"report","arguments":["arrived"]},{"action":"work","arguments":[40]}],"transitions":[{"target":"done","conditions":[{"left":{"observation":"work_done"},"op":"eq","right":true},{"left":{"observation":"answer"},"op":"eq","right":"yes"}]}]},{"id":"done","entry":[{"action":"report","arguments":["done"]},{"action":"complete","arguments":["repaired"]}]}]})"));
	auto &map = source["maps"][0];
	map["entities"] =
	    Json::array({{{"id", 1}, {"definition", "test.away"}, {"x_mm", 10000}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	map["ship_slots"] = Json::array(
	    {{{"id", "crew"}, {"allowed_ships", {"test.away"}}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}}});
	map["conversations"] =
	    Json::array({{{"id", "channel"},
	                  {"tree", "test.dialogue"},
	                  {"bindings", {{"ship", {{"slot", "crew"}}}, {"team", {{"entity", 1}, {"character", "leader"}}}}},
	                  {"members", {"ship", "team"}}}});
	map["away_missions"] =
	    Json::array({{{"id", "job"},
	                  {"title_key", "Repair job"},
	                  {"ship_slot", "crew"},
	                  {"target_entity", 1},
	                  {"conversation", "channel"},
	                  {"reveal_event", "Mission available"},
	                  {"tree", "test.away-tree"},
	                  {"slots", Json::array({{{"id", "repair"}, {"kind", "team"}, {"required", true}}})}}});
	return source;
}
void ticks(World &w, int n)
{
	for (int i = 0; i < n; ++i)
		w.tick();
}
Json board(const World &w, std::uint64_t claim = 101)
{
	return w.observe(claim)->gameplay.at("away_missions");
}
OperatingCommand order(const World &w, const std::string &action, const std::string &text = {},
                       std::uint64_t claim = 101, const std::string &mission = "job")
{
	const auto o = w.observe(claim);
	Json row;
	for (const auto &r : board(w, claim))
		if (r.at("id") == mission)
			row = r;
	OperatingCommand c{o->own.id, "away." + action, row.at("revision"), mission};
	c.console = o->console;
	c.controller = claim == 101 ? (action == "transport" ? "transport" : "dispatch") : "team";
	c.text = text;
	for (const auto &ctrl : o->gameplay.at("controllers"))
		if (ctrl.at("id") == c.controller)
			c.generation = ctrl.at("generation");
	return c;
}
int main(int argc, char **argv)
{
	try
	{
		// Support the save/resume contract at the shared effect-journal boundary.
		std::set<std::string> receipts;
		state_tree::Output effect;
		effect.task = 1;
		effect.call_path = {2};
		effect.call_activations = {1};
		require(admit_effect(receipts, effect), "policy effect admitted");
		require(admit_effect(receipts, effect, "action/1/"), "action effect is independent of policy effect");
		require(!admit_effect(receipts, effect, "action/1/"), "same action effect is not replayed");
		require(admit_effect(receipts, effect, "action/2/"), "new action instance can repeat its effect");
		receipts.clear();
		for (std::size_t i = 1; i <= effect_receipt_limit; ++i)
		{
			effect.activation = i;
			require(admit_effect(receipts, effect), "distinct tree activation admitted");
		}
		require(!admit_effect(receipts, effect), "replay remains harmless at journal capacity");
		++effect.call_activations[0];
		rejects(
		    [&]
		    {
			    admit_effect(receipts, effect);
		    },
		    "new nested activation refused before journal overflow");
		require(receipts.size() == effect_receipt_limit, "refused effect keeps journal within save bounds");
		auto input = fixture();
		auto bad = input;
		bad["entities"][0]["personnel"]["officers"][0]["traits"] = Json::array();
		rejects(
		    [&]
		    {
			    load(bad);
		    },
		    "unqualified starting Navigator rejected");
		bad = input;
		bad["trees"][bad["trees"].size() - 2]["prompts"]["choice"].erase("timeout_ticks");
		rejects(
		    [&]
		    {
			    load(bad);
		    },
		    "away choice requires timeout");
		const auto catalog = load(input);
		World w;
		w.load(catalog, "test.map", {{"crew", "test.away"}});
		const auto ship = *w.ship_for_slot("crew");
		require(w.claim(101, ship, "drive") && w.claim(102, ship, "observer"), "claims");
		ticks(w, 4);
		require(board(w).size() == 1, "delivered introduction discloses mission");
		auto projected = network::read_observation(network::observation_json(*w.observe(101)), catalog);
		require(projected.gameplay.at("away_missions") == board(w), "network mission projection round trip");
		require(!w.submit(102, order(w, "open", "", 102)), "only Dispatch opens");
		require(w.submit(101, order(w, "open")), "open");
		ticks(w, 1);
		require(!w.submit(101, order(w, "assign", "repair/team:team")), "Helm cannot take Engineering team");
		require(!w.submit(101, order(w, "send")), "mandatory slot required");
		require(w.submit(102, order(w, "assign", "repair/team:team", 102)), "assign team");
		ticks(w, 1);
		require(w.submit(101, order(w, "transport", "transport")), "transport binding");
		ticks(w, 12);
		auto personnel = [&](const std::string &action, const std::string &duty)
		{
			OperatingCommand c{ship, "personnel." + action, 0, "nav"};
			c.text = duty;
			c.generation = w.observe(101)->gameplay.at("personnel").at("revision");
			return w.submit(101, c);
		};
		require(personnel("relieve", ""), "relieve Navigator");
		ticks(w, 1);
		require(w.submit(101, order(w, "send")), "queue transport while Navigator absent");
		ticks(w, 5);
		require(board(w)[0]["phase"] == "outbound", "unqualified transport must wait");
		require(w.submit(101, order(w, "cancel")), "cancel pending transport");
		ticks(w, 1);
		require(personnel("assign", "transport"), "restore Navigator");
		ticks(w, 2);
		auto stale = order(w, "send");
		require(w.submit(101, stale), "send");
		ticks(w, 1);
		require(!w.submit(101, stale), "stale manifest rejected");
		auto saved = w.save();
		require(saved["away_missions"][0]["schema"] == 2 && !saved["away_missions"][0].contains("deployed"),
		        "deployment is derived from the typed phase");
		auto legacy = saved;
		legacy["away_missions"][0]["schema"] = 1;
		legacy["away_missions"][0]["deployed"] = false;
		legacy["away_missions"][0].erase("repair_compartment");
		World migrated;
		migrated.restore(catalog, legacy);
		require(migrated.save() == saved, "valid legacy away state migrates without changing continuation");
		auto invalid = saved;
		invalid["away_missions"][0]["schema"] = 1;
		invalid["away_missions"][0]["deployed"] = true;
		rejects(
		    [&]
		    {
			    World rejected;
			    rejected.restore(catalog, invalid);
		    },
		    "inconsistent deployment rejected");
		invalid = saved;
		invalid["away_missions"][0]["phase"] = "unknown";
		rejects(
		    [&]
		    {
			    World rejected;
			    rejected.restore(catalog, invalid);
		    },
		    "unknown away phase rejected");
		World resumed;
		invalid = saved;
		invalid["away_missions"][0]["accompanying"] = Json::array();
		rejects(
		    [&]
		    {
			    World rejected;
			    rejected.restore(catalog, invalid);
		    },
		    "missing attached officer rejected");
		resumed.restore(catalog, saved);
		ticks(w, 10);
		ticks(resumed, 10);
		require(w.save() == resumed.save(), "transfer and prompt continuation");
		require(board(w)[0]["phase"] == "deployed", "physical deployment");
		require(w.save()["away_missions"][0]["accompanying"].size() == 1, "team officer accompanies");
		auto radio = [&](int enabled)
		{
			OperatingCommand c{ship, {}, enabled, {}};
			c.console = "drive";
			c.controller = "radio";
			c.channel = "enabled";
			const auto observed = w.observe(101);
			for (const auto &ctrl : observed->gameplay.at("controllers"))
				if (ctrl.at("id") == "radio")
					c.generation = ctrl.at("generation");
			return w.submit(101, c);
		};
		require(radio(0), "disable return Comms link");
		ticks(w, 1);
		require(w.submit(101, order(w, "recall")), "recall");
		ticks(w, 5);
		require(board(w)[0]["phase"] == "recovered", "recovered");
		const auto progress = w.save()["away_missions"][0]["progress"];
		ticks(w, 8);
		require(w.save()["away_missions"][0]["progress"] == progress, "no remote work after recovery");
		require(w.submit(102, order(w, "assign", "repair/team:team", 102)), "reassign recovered team");
		ticks(w, 2);
		require(w.submit(101, order(w, "send")), "resume send");
		ticks(w, 70);
		require(w.save()["away_missions"][0]["outcome"] == "complete:repaired", "work and timeout finish");
		require(board(w)[0]["phase"] == "deployed", "completion does not teleport crew home");
		require(!board(w)[0].contains("outcome"), "Dispatch does not expose hidden remote outcome");
		require(radio(1), "restore Comms link");
		ticks(w, 3);
		const auto comms = w.observe(101)->gameplay.at("conversations");
		bool report = false;
		for (const auto &thread : comms.at("threads"))
			for (const auto &message : thread.at("messages"))
				if (message.at("text") == "Work complete")
					report = true;
		require(report, "deferred completion report delivered");
		if (argc > 1 && std::string(argv[1]) == "--trace")
		{
			std::cout << w.save().at("away_missions").dump() << '\n';
			return 0;
		}
		auto shared_input = fixture();
		auto &officers = shared_input["entities"][0]["personnel"]["officers"];
		officers.push_back({{"id", "reserve"},
		                    {"name", "Reserve specialist"},
		                    {"department", "engineering"},
		                    {"compartment", "bridge"},
		                    {"traits", {"repair"}}});
		auto second = shared_input["maps"][0]["away_missions"][0];
		second["id"] = "job2";
		second["slots"] =
		    Json::array({{{"id", "expert"}, {"kind", "officer"}, {"required", true}, {"trait", "repair"}}});
		shared_input["maps"][0]["away_missions"].push_back(second);
		auto shared_catalog = load(shared_input);
		World shared;
		shared.load(shared_catalog, "test.map", {{"crew", "test.away"}});
		auto shared_ship = *shared.ship_for_slot("crew");
		require(shared.claim(101, shared_ship, "drive") && shared.claim(102, shared_ship, "observer"), "shared claims");
		ticks(shared, 4);
		for (const auto &job : {"job", "job2"})
		{
			require(shared.submit(101, order(shared, "open", "", 101, job)), "open shared job");
			ticks(shared, 1);
			require(shared.submit(101, order(shared, "transport", "transport", 101, job)), "share transport binding");
			ticks(shared, 1);
		}
		require(shared.submit(102, order(shared, "assign", "repair/team:team", 102)), "shared team");
		ticks(shared, 1);
		require(!shared.submit(102, order(shared, "assign", "expert/officer:chief", 102, "job2")),
		        "attached officer cannot be double-booked");
		require(shared.submit(102, order(shared, "assign", "expert/officer:reserve", 102, "job2")),
		        "independent officer slot");
		ticks(shared, 12);
		require(shared.submit(101, order(shared, "send")) && shared.submit(101, order(shared, "send", "", 101, "job2")),
		        "queue shared transfers");
		ticks(shared, 2);
		require(shared.save()["away_missions"][0]["transfer"] == 2 &&
		            shared.save()["away_missions"][1]["transfer"] == 0,
		        "one active transfer in acceptance order");
		ticks(shared, 5);
		require(board(shared)[0]["phase"] == "deployed" && board(shared)[1]["phase"] == "deployed",
		        "shared system deploys both missions serially");
		// Remote repair uses real team rates and leaves actual, resumable HP changes.
		auto repair_input = fixture();
		auto casualty = repair_input["entities"][0];
		casualty["id"] = "test.casualty";
		casualty["combat"]["decks"][0]["compartments"][0]["initial_hp"] = 0;
		casualty["combat"]["decks"][0]["compartments"][0]["recovery_hp"] = 50;
		casualty["systems"]["radio"]["compartment"] = "workshop";
		for (auto &ctrl : casualty["control_layout"]["controllers"])
			if (ctrl["id"] == "team")
				ctrl["commands"][0]["initial"] = 0;
		repair_input["entities"].push_back(casualty);
		repair_input["maps"][0]["entities"][0]["definition"] = "test.casualty";
		auto &work_entry = repair_input["trees"].back()["states"][1]["entry"];
		work_entry[2]["arguments"] = {1};
		work_entry.push_back({{"action", "restore_compartment"}, {"arguments", {"bridge"}}});
		const auto repair_catalog = load(repair_input);
		auto slow_input = repair_input;
		slow_input["entities"][0]["systems"]["team"]["hp_per_second"] = 10;
		const auto slow_catalog = load(slow_input);
		auto deploy = [&](World &world, const Catalog &content)
		{
			world.load(content, "test.map", {{"crew", "test.away"}});
			const auto own = *world.ship_for_slot("crew");
			world.claim(101, own, "drive");
			world.claim(102, own, "observer");
			ticks(world, 4);
			require(world.submit(101, order(world, "open")), "open repair job");
			ticks(world, 1);
			require(world.submit(102, order(world, "assign", "repair/team:team", 102)), "assign repair team");
			ticks(world, 1);
			require(world.submit(101, order(world, "transport", "transport")), "bind repair transport");
			ticks(world, 12);
			require(world.submit(101, order(world, "send")), "deploy repair team");
			ticks(world, 5);
		};
		auto repaired_compartment = [](const World &world)
		{
			const auto saved = world.save();
			for (const auto &body : saved.at("bodies"))
				if (body.at("body").at("id") == 1)
					return body.at("combat").at("compartments")[0];
			throw std::runtime_error("Missing casualty");
		};
		World repair, slow;
		deploy(repair, repair_catalog);
		deploy(slow, slow_catalog);
		ticks(repair, 10);
		ticks(slow, 10);
		const auto partial = repaired_compartment(repair);
		require(partial.at("hp") > repaired_compartment(slow).at("hp") && partial.at("hp") < 100,
		        "authored repair rate controls partial remote restoration");
		require(partial.at("disabled") && repair.save()["away_missions"][0]["outcome"] == "",
		        "work budget alone cannot bypass structural recovery or complete repair");
		World repair_resumed;
		repair_resumed.restore(repair_catalog, repair.save());
		ticks(repair, 8);
		ticks(repair_resumed, 8);
		require(repair.save() == repair_resumed.save(), "partial remote repair resumes exactly");
		require(repair.submit(101, order(repair, "recall")), "recall during structural repair");
		ticks(repair, 5);
		const auto paused_hp = repaired_compartment(repair);
		ticks(repair, 10);
		require(repaired_compartment(repair) == paused_hp, "recovered team stops remote repair");
		require(repair.submit(102, order(repair, "assign", "repair/team:team", 102)), "reassign repair party");
		ticks(repair, 2);
		require(repair.submit(101, order(repair, "send")), "redeploy repair party");
		ticks(repair, 200);
		require(repaired_compartment(repair).at("hp") == 100 && !repaired_compartment(repair).at("disabled") &&
		            repair.save()["away_missions"][0]["outcome"] == "complete:repaired",
		        "physical repairs finish before mission completion");
		// Staging uses the same enabled gate as ordinary repair-team travel.
		World staging;
		staging.load(catalog, "test.map", {{"crew", "test.away"}});
		const auto staging_ship = *staging.ship_for_slot("crew");
		staging.claim(101, staging_ship, "drive");
		staging.claim(102, staging_ship, "observer");
		ticks(staging, 4);
		auto team_enabled = [&](int value)
		{
			OperatingCommand c{staging_ship, {}, value, {}};
			c.controller = "team";
			c.channel = "enabled";
			const auto observation = staging.observe(102);
			for (const auto &ctrl : observation->gameplay.at("controllers"))
				if (ctrl.at("id") == "team")
					c.generation = ctrl.at("generation");
			return staging.submit(102, c);
		};
		require(team_enabled(0), "disable team before reservation");
		ticks(staging, 1);
		staging.submit(101, order(staging, "open"));
		ticks(staging, 1);
		staging.submit(102, order(staging, "assign", "repair/team:team", 102));
		ticks(staging, 1);
		staging.submit(101, order(staging, "transport", "transport"));
		ticks(staging, 12);
		require(!staging.submit(101, order(staging, "send")), "disabled team cannot walk to transport");
		require(staging.submit(102, order(staging, "withdraw", "repair", 102)), "release disabled team");
		ticks(staging, 1);
		require(team_enabled(1), "enable released team");
		ticks(staging, 1);
		staging.submit(102, order(staging, "assign", "repair/team:team", 102));
		ticks(staging, 12);
		require(staging.submit(101, order(staging, "send")), "enabled team reaches transport");
		auto ranged = fixture();
		ranged["entities"][0]["systems"]["transport"]["range_mm"] = 1000;
		auto ranged_catalog = load(ranged);
		World far;
		far.load(ranged_catalog, "test.map", {{"crew", "test.away"}});
		auto far_ship = *far.ship_for_slot("crew");
		far.claim(101, far_ship, "drive");
		far.claim(102, far_ship, "observer");
		ticks(far, 4);
		far.submit(101, order(far, "open"));
		ticks(far, 1);
		far.submit(102, order(far, "assign", "repair/team:team", 102));
		ticks(far, 1);
		far.submit(101, order(far, "transport", "transport"));
		ticks(far, 12);
		require(far.submit(101, order(far, "send")), "queue out-of-range transfer");
		ticks(far, 5);
		require(far.save()["away_missions"][0]["transfer"] == 0, "no out-of-range displacement");
		std::cout << "PASS away authority, staging, transport, Comms default, recovery and continuation\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
