#include "../support/assertions.hpp"
#include "../support/away_fixture.hpp"
#include <iostream>
import phoenix.content.controllers;
import phoenix.content.scenarios;
import phoenix.simulation;
import phoenix.networking.projection;
import phoenix.effects.contracts;
using namespace phoenix;
using namespace phoenix::test;

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
#include "../support/away_priority_cases.hpp"
#include "../support/away_medical_cases.hpp"
void assisted_docking_mission()
{
	auto source = away_fixture();
	auto &hull = source["entities"][0];
	hull["tags"] = {"ship"};
	hull["systems"]["scan"] = {{"kind", "radar"}, {"range_mm", 100000}, {"shows", {"ship"}}, {"compartment", "bridge"}};
	hull["control_layout"]["controllers"].push_back(
	    {{"id", "scan"},
	     {"console", "drive"},
	     {"fallback", "hold"},
	     {"commands",
	      Json::array({{{"id", "enabled"}, {"action", "system_enabled"}, {"initial", 1}, {"consumers", {"scan"}}}})}});
	hull["control_layout"]["profiles"][0]["consoles"]["drive"]["human"].push_back("scan");
	hull["stations"][0]["consoles"][0]["feeds"].push_back("scan");
	hull["personnel"]["duties"].push_back({{"id", "scan"},
	                                       {"label", "Scan"},
	                                       {"controller", "scan"},
	                                       {"console", "drive"},
	                                       {"department", "navigation"},
	                                       {"compartment", "bridge"},
	                                       {"effects", Json::object()}});
	hull["transport"] = {{"compartment", "bridge"}, {"capacity", 8}, {"capabilities", {"shelter"}}};
	hull["docking"] = Json::parse(
	    R"({"ports":[{"id":"fore","controller":"dispatch","compartment":"bridge","compatibility":"standard","x_mm":5000,"heading_mdeg":0,"mating_ticks":3},{"id":"aft","controller":"dispatch","compartment":"bridge","compatibility":"standard","x_mm":-5000,"heading_mdeg":180000,"mating_ticks":3}]})");
	auto &layout = hull["control_layout"];
	layout["station_complexity"] = true;
	layout["profiles"][0]["stars"] = 3;
	auto low = layout["profiles"][0];
	low["id"] = "assisted";
	low["stars"] = 1;
	low["consoles"]["observer"]["human"] = Json::array();
	low["consoles"]["observer"]["ai"] = {"team"};
	layout["profiles"].push_back(low);
	layout["default_profile"] = "assisted";
	auto &mission = source["maps"][0]["away_missions"][0];
	mission["allow_ship_transport"] = true;
	mission["docking_ports"] = {"aft"};
	mission["deployment_ticks"] = 3;
	auto target = hull;
	target["id"] = "test.receiving-ship";
	target["control_layout"]["default_profile"] = "direct";
	source["entities"].push_back(target);
	source["maps"][0]["entities"][0]["definition"] = "test.receiving-ship";
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	const auto ship = *world.ship_for_slot("crew");
	require(world.claim(101, ship, "drive") && world.claim(102, ship, "observer") && world.claim(103, 1, "drive"),
	        "Claim occupied assisted watches and receiving Helm");
	ticks(world, 4);
	require(board(world)[0].at("automatic_dispatch") == true, "One-star docking activation is projected");
	// Native workflow envelopes cannot smuggle Controller/message payloads.
	// Exercise host admission through the same valid, claimed synthetic mission.
	const auto before_rejected_envelopes = world.save();
	for (int field = 0; field != 5; ++field)
	{
		auto malformed = order(world, "open");
		switch (field)
		{
		case 0:
			malformed.channel = "unexpected";
			break;
		case 1:
			malformed.request_id = 42;
			break;
		case 2:
			malformed.arguments = {1};
			break;
		case 3:
			malformed.speaker_name = "forged";
			break;
		case 4:
			malformed.speaker_avatar = {1};
			break;
		}
		require(!world.submit(101, malformed), "native envelope refuses unrelated payload fields");
	}
	require(world.save() == before_rejected_envelopes, "rejected native envelopes leave authoritative state unchanged");
	require(world.submit(101, order(world, "open")), "Activate assisted docking mission");
	ticks(world, 20);
	const auto prepared = board(world)[0];
	require(prepared["slots"][0]["assigned"] == "team:team" && !prepared["accompanying"].empty(),
	        "AI-operated team Controller prepares team and duty officer despite occupied Console");
	require(prepared["phase"] == "open" && prepared["transport"] == "ship",
	        "Activated party waits for real docking without dispatching or launching a shuttle");
	require(network::read_observation(network::observation_json(*world.observe(101)), catalog)
	                .gameplay.at("away_missions") == board(world),
	        "Assisted activation survives remote projection");
	require(world.submit(101, order(world, "cancel")), "Cancel activated mission while waiting for docking");
	ticks(world, 10);
	require(board(world)[0]["phase"] == "available" && board(world)[0]["slots"][0]["assigned"] == "",
	        "Cancellation removes activation and prevents automatic redeployment");
	require(world.submit(101, order(world, "open")), "Reactivate cancelled docking mission");
	ticks(world, 20);
	World resumed;
	resumed.restore(catalog, world.save());
	auto dock =
	    [&](World &w, std::uint64_t route, const std::string &verb, const std::string &port, const std::string &text)
	{
		const auto view = *w.observe(route);
		OperatingCommand command{view.own.id, "dock." + verb, view.gameplay.at("docking").at("revision"), port};
		command.console = "drive";
		command.controller = "dispatch";
		command.text = text;
		for (const auto &controller : view.gameplay.at("controllers"))
			if (controller.at("id") == "dispatch")
				command.generation = controller.at("generation");
		return w.submit(route, command);
	};
	for (auto *w : {&world, &resumed})
	{
		require(dock(*w, 103, "accept", "aft", std::to_string(ship)), "Accept assisted docking");
		ticks(*w, 1);
		require(dock(*w, 101, "connect", "fore", "1/aft"), "Helm deliberately docks activated mission");
		ticks(*w, 12);
		require(board(*w)[0]["phase"] == "deployed", "Docking dispatches prepared one-star party without Send");
		require(w->submit(101, order(*w, "recall")), "Helm deliberately recalls assisted party");
		ticks(*w, 20);
		require(board(*w)[0]["phase"] == "recovered", "Recovered party does not redeploy automatically");
	}
	require(world.save() == resumed.save(), "Activated docking mission resumes without repeated assignments");
}
void away_effect_actions()
{
	auto source = away_fixture();
	source["properties"] = Json::parse(R"json([
 {"id":"test.away.events","kind":"quantity","unit":"events","default":0,"maximum":1000}
 ])json");
	source["entities"][0]["properties"] = {{"test.away.events", Json::object()}};
	source["effects"] = Json::parse(R"json([
 {"id":"test.away.base","kind":"operations","operations":[{"kind":"property_add","target":"target","property":"test.away.events","amount":7}]},
 {"id":"test.away.add","extends":"test.away.base"},
 {"id":"test.away.self","kind":"operations","operations":[{"kind":"property_add","target":"target","property":"test.away.events","amount":3}]},
 {"id":"test.away.fail","kind":"operations","costs":[{"payer":"owner","property":"test.away.events","amount":100}],"operations":[{"kind":"property_add","target":"target","property":"test.away.events","amount":90}]}
 ])json");
	Json *tree = nullptr;
	for (auto &definition : source["trees"])
		if (definition.at("id") == "test.away-tree")
			tree = &definition;
	require(tree != nullptr, "Away synthetic tree present");
	auto &tasks = (*tree)["states"][1]["running"] = Json::array();
	for (const auto &[group, role] :
	     {std::pair{"test.away.add", "target"}, {"test.away.self", "self"}, {"test.away.fail", "target"}})
		tasks.push_back({{"action", "apply_effect_group"}, {"arguments", {group, role}}});
	auto invalid = source;
	for (auto &definition : invalid["trees"])
		if (definition.at("id") == "test.away-tree")
			definition["states"][1]["running"][0]["arguments"][0] = "test.missing";
	rejects(
	    [&]
	    {
		    (void)load(invalid);
	    },
	    "Away effect reference validated through production loading");
	invalid = source;
	for (auto &definition : invalid["trees"])
		if (definition.at("id") == "test.away-tree")
			definition["states"][1]["running"][0]["arguments"][1] = "hidden";
	rejects(
	    [&]
	    {
		    (void)load(invalid);
	    },
	    "Away effect role validated through production loading");
	const auto resolved = load(source);
	const auto closure = scenario_definitions(resolved, "test.map");
	for (const auto *id : {"test.away.add", "test.away.events"})
		require(std::find(closure.begin(), closure.end(), id) != closure.end(),
		        "Away literal effects and transitive properties enter scenario export closure");
	// Exported runtime definitions are resolved; inheritance has already been flattened.
	auto pack = fixture_pack(json_bytes(source), "test-core");
	auto exported = Json::parse(pack.files.at("definitions.json"));
	exported["effects"] = Json::array();
	for (const auto &id : closure)
		if (resolved.effects.contains(id))
			exported["effects"].push_back(resolved.effects.at(id));
	pack.files["definitions.json"] = json_bytes(exported);
	const auto catalog = resolve_packs({read_pack(write_pack(pack))});
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	const auto ship = *world.ship_for_slot("crew");
	require(world.claim(101, ship, "drive") && world.claim(102, ship, "observer"), "Away effect claims");
	ticks(world, 4);
	require(world.submit(101, order(world, "open")), "Open away effect mission");
	ticks(world, 1);
	require(world.submit(102, order(world, "assign", "repair/team:team", 102)), "Assign away effect team");
	ticks(world, 1);
	require(world.submit(101, order(world, "transport", "transport")), "Select away effect transport");
	ticks(world, 12);
	require(world.submit(101, order(world, "send")), "Send away effect party");
	ticks(world, 6);
	const auto count = [](const World &w, EntityId entity) -> Json
	{
		const auto saved = w.save();
		for (const auto &row : saved.at("custom_properties"))
			if (row.at("entity") == entity && row.at("equipment") == "" && row.at("compartment") == -1)
				return row.at("values").at("test.away.events");
		throw std::runtime_error("Missing away effect counter");
	};
	require(count(world, 1) == 7 && count(world, ship) == 3,
	        "Away effects bind actual destination and home source; failed group is atomic");
	const auto saved = world.save();
	require(saved.at("rules").at("results").size() == 1, "Away failure produces one bounded receipt");
	World restored;
	restored.restore(catalog, saved);
	ticks(world, 9);
	ticks(restored, 9);
	require(world.save() == restored.save(), "Away effect success/failure continuation restores exactly");
	require(count(world, 1) == 7 && count(world, ship) == 3 && world.save().at("rules").at("results").size() == 1,
	        "Running effect tasks and failed payments do not replay after restoration");
}

int main(int argc, char **argv)
{
	try
	{
		assisted_docking_mission();
		away_effect_actions();
		away_priority_cases();
		away_medical_cases();
		{
			auto routed = away_fixture();
			auto &hull = routed["entities"][0];
			hull["systems"]["engineering-dispatch"] = {{"kind", "dispatch"}, {"compartment", "workshop"}};
			hull["stations"][1]["consoles"][0]["feeds"].push_back("engineering-dispatch");
			hull["control_layout"]["controllers"].push_back(
			    {{"id", "engineering-dispatch"},
			     {"console", "observer"},
			     {"fallback", "hold"},
			     {"commands", Json::array({{{"id", "enabled"},
			                                {"action", "system_enabled"},
			                                {"initial", 1},
			                                {"consumers", {"engineering-dispatch"}}}})}});
			hull["control_layout"]["profiles"][0]["consoles"]["observer"]["human"].push_back("engineering-dispatch");
			hull["personnel"]["duties"].push_back({{"id", "engineering-dispatch"},
			                                       {"label", "Engineering Dispatch"},
			                                       {"controller", "engineering-dispatch"},
			                                       {"console", "observer"},
			                                       {"department", "engineering"},
			                                       {"compartment", "workshop"}});
			routed["maps"][0]["away_missions"][0]["dispatch"] = "engineering-dispatch";
			World departments;
			departments.load(load(routed), "test.map", {{"crew", "test.away"}});
			const auto origin = *departments.ship_for_slot("crew");
			require(departments.claim(101, origin, "drive") && departments.claim(102, origin, "observer"),
			        "department claims");
			ticks(departments, 4);
			require(!board(departments, 101)[0]["dispatch"].get<bool>() &&
			            board(departments, 102)[0]["dispatch"].get<bool>(),
			        "mission lifecycle belongs to its routed Dispatch");
			require(!departments.submit(101, order(departments, "open")), "other Dispatch cannot open routed mission");
			auto command = order(departments, "open", "", 102);
			command.controller = "engineering-dispatch";
			const auto engineering = departments.observe(102);
			for (const auto &controller : engineering->gameplay.at("controllers"))
				if (controller.at("id") == command.controller)
					command.generation = controller.at("generation");
			require(departments.submit(102, command), "owning Engineering Dispatch opens mission");
			ticks(departments, 1);
			command.action = "away.cancel";
			command.value = board(departments, 102)[0].at("revision");
			require(departments.submit(102, command), "owning Dispatch cancels preparation");
			ticks(departments, 1);
			require(board(departments, 102)[0].at("phase") == "available",
			        "cancelled preparation releases contributions");
		}
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
		auto input = away_fixture();
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
		require(saved["away_missions"][0]["schema"] == 11 && !saved["away_missions"][0].contains("deployed"),
		        "deployment is derived from the typed phase");
		for (const auto schema : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12})
		{
			auto retired = saved;
			retired["away_missions"][0]["schema"] = schema;
			rejects(
			    [&]
			    {
				    World rejected;
				    rejected.restore(catalog, retired);
			    },
			    "non-current away schema rejected within a current World envelope");
		}
		auto invalid = saved;
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
		auto shared_input = away_fixture();
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
		auto repair_input = away_fixture();
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
		auto ranged = away_fixture();
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

		{
			auto protected_input = away_fixture();
			protected_input["effects"] =
			    Json::parse(R"json([{"id":"test.away-shield","kind":"shield","duration_ticks":30,
              "contributions":{"bubble":{"capacity":10,"initial":10,"sustain_energy_per_second":0,
                "recharge_per_second":0,"off_recharge_per_second":0,"warmup_ticks":1}}}])json");
			protected_input["maps"][0]["scenario"] = {{"tree", "test.protect-away-target"},
			                                          {"bindings", {{"actor", {{"entity", 1}}}}}};
			protected_input["trees"].push_back(
			    Json::parse(R"json({"id":"test.protect-away-target","context":"scenario","initial":"grant",
              "states":[{"id":"grant","entry":[{"action":"apply_shield_effect","arguments":[{"observation":"self.entity"},"test.away-shield","barrier"]}]}]})json"));
			const auto protected_catalog = load(protected_input);
			World protected_world;
			protected_world.load(protected_catalog, "test.map", {{"crew", "test.away"}});
			const auto crew = *protected_world.ship_for_slot("crew");
			require(protected_world.claim(101, crew, "drive") && protected_world.claim(102, crew, "observer"),
			        "claim shield-blocked transport seats");
			ticks(protected_world, 4);
			require(protected_world.submit(101, order(protected_world, "open")), "open protected target mission");
			ticks(protected_world, 1);
			require(protected_world.submit(102, order(protected_world, "assign", "repair/team:team", 102)),
			        "stage team for shield-blocked target");
			ticks(protected_world, 1);
			require(protected_world.submit(101, order(protected_world, "transport", "transport")),
			        "select displacement transport");
			ticks(protected_world, 12);
			require(board(protected_world)[0]["blocker"] == "Lower shields for displacement",
			        "non-equipment shield must block the ordinary displacement path");
			require(!protected_world.submit(101, order(protected_world, "send")),
			        "shielded target refuses normal displacement command");
			require(protected_world.save()["away_missions"][0]["transfer"] == 0,
			        "shielded target cannot advance displacement");
			ticks(protected_world, 13);
			require(board(protected_world)[0]["blocker"] == "",
			        "expired scenario contribution releases displacement blocker");
			require(protected_world.submit(101, order(protected_world, "send")),
			        "normal displacement admitted after scenario shield expiry");
			ticks(protected_world, 4);
			require(protected_world.save()["away_missions"][0]["phase"] == "deployed",
			        "ordinary displacement reaches target once its independent shield expires");
		}
		std::cout << "PASS away authority, staging, transport, Comms default, recovery and continuation\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
