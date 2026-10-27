module;
#include <utility>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.content;
namespace phoenix
{
state_tree::Registry away_registry(const Json &tree)
{
	using namespace state_tree;
	Registry r;
	for (const auto &name : {"deployed", "recovered", "work_done"})
		r.observations.push_back({name, Type::Boolean, 16, {}});
	for (const auto &name : {"tick", "progress"})
		r.observations.push_back({name, Type::Integer, 16, {}});
	for (const auto &name : {"answer", "checkpoint"})
		r.observations.push_back({name, Type::Text, 16, {}});
	for (const auto &name : {"transport_present", "transport_available"})
		r.observations.push_back({name, Type::Boolean, 16, {}});
	std::set<std::string> capabilities;
	for (const auto &tag : tree.value("transport_capabilities", Json::array()))
	{
		const auto key = tag.get<std::string>();
		if (key.empty() || key.size() > 64 ||
		    key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
		        std::string::npos ||
		    !capabilities.insert(key).second)
			throw std::runtime_error("Invalid away transport capability");
		r.observations.push_back({"transport.capability." + key, Type::Boolean, 16, {}});
		r.observations.push_back({"transport.operational." + key, Type::Boolean, 16, {}});
	}
	for (const auto *category : {"flags", "team_capabilities", "events"})
	{
		std::set<std::string> unique;
		const auto values = tree.value(category, std::vector<std::string>{});
		if (values.size() > 64)
			throw std::runtime_error("Too many away observations");
		for (const auto &key : values)
		{
			if (key.empty() || key.size() > 64 || !unique.insert(key).second)
				throw std::runtime_error("Invalid away observation");
			r.observations.push_back({std::string(category) + "." + key, Type::Boolean, 16, {}});
		}
	}
	for (const auto &name :
	     {"report", "checkpoint", "emit", "complete", "fail", "restore_compartment", "release_transport", "offer",
	      "say", "mark", "pause", "log", "injure_duty", "kill_duty", "require_team", "claim"})
		r.actions.push_back({name, 16, {Type::Text}, true});
	r.actions.push_back({"work", 16, {Type::Integer}, true});
	r.actions.push_back({"timed_work", 16, {Type::Integer}, true});
	r.actions.push_back({"crew_deaths", 16, {Type::Integer}, true});
	if (tree.value("boarding", false))
	{
		for (const auto *name : {"arrived", "encounter_done", "encounter_success"})
			r.observations.push_back({name, Type::Boolean, 16, {}});
		r.observations.push_back({"defenders", Type::Integer, 16, {}});
		for (const auto *name : {"move_to", "encounter", "secure", "release", "capture"})
			r.actions.push_back({name, 16, {Type::Text}, true});
	}
	return r;
}
std::vector<AwayDefinition> compile_away(const Catalog &catalog, const Json &map)
{
	const auto directed_catalog = compile_directed_catalog(catalog);
	return compile_away(catalog, map, directed_catalog);
}
std::vector<AwayDefinition> compile_away(const Catalog &catalog, const Json &map,
                                         const DirectedCatalog &directed_catalog, SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	auto require = [](bool ok, const std::string &why)
	{
		if (!ok)
			throw std::runtime_error("away missions: " + why);
	};
	std::vector<AwayDefinition> out;
	std::set<std::string> ids;
	auto rows = map.value("away_missions", Json::array());
	require(rows.is_array() && rows.size() <= 32, "expected at most 32 missions");
	for (const auto &j : rows)
	{
		AwayDefinition d;
		d.id = j.at("id");
		d.title = j.at("title_key");
		d.slot = j.at("ship_slot");
		d.dispatch = j.value("dispatch", std::string{});
		require(d.dispatch.size() <= 128, "invalid Dispatch route");
		if (!d.dispatch.empty())
		{
			bool found = false;
			for (const auto &slot : map.at("ship_slots"))
				if (slot.at("id") == d.slot)
					for (const auto &hull : slot.at("allowed_ships"))
					{
						bool fitted = false;
						for (const auto &system : resolve_systems(catalog, hull.get<std::string>()))
							fitted |= system.id == d.dispatch && system.kind == OperatingSystem::Dispatch;
						require(fitted, "Dispatch route missing from allowed ship");
						found = true;
					}
			require(found, "unknown fitted Dispatch route");
		}
		d.target = j.at("target_entity");
		std::set<std::string> craft_slots;
		int transports = 0;
		for (const auto &slot : j.value("craft_slots", Json::array()))
		{
			AwayCraftSlot s{slot.at("id"), slot.at("role"), slot.value("craft_role", std::string{}),
			                slot.value("required", false)};
			require(!s.id.empty() && s.id.size() <= 64 && craft_slots.insert(s.id).second &&
			            (s.role == "transport" || s.role == "escort") &&
			            (s.craft_role.empty() || s.craft_role == "shuttle" || s.craft_role == "interceptor" ||
			             s.craft_role == "boarding_torpedo"),
			        "invalid craft slot");
			if (s.role == "transport")
			{
				++transports;
				require(s.required, "primary transport must be required");
			}
			else
				require(s.craft_role == "interceptor", "escort must be an interceptor");
			d.craft_slots.push_back(s);
		}
		require(d.craft_slots.size() <= 9 && (d.craft_slots.empty() || transports == 1),
		        "expected one primary transport and up to eight escorts");
		if (j.contains("supply"))
		{
			const auto &s = j.at("supply");
			SupplyOffer offer{s.at("provider_store"),
			                  s.value("home_store", std::string{"main-hold"}),
			                  s.at("material"),
			                  s.value("payment", std::string{}),
			                  s.at("limit"),
			                  s.value("give", std::int64_t{1}),
			                  s.value("pay", std::int64_t{0})};
			require(!offer.material.empty() && offer.limit > 0 && offer.limit <= 1000000000 && offer.give > 0 &&
			            offer.give <= offer.limit && offer.pay >= 0 && offer.pay <= 1000000000 &&
			            (offer.pay == 0) == offer.payment.empty() && offer.payment != offer.material,
			        "invalid supply terms");
			require(!d.craft_slots.empty() && std::any_of(d.craft_slots.begin(), d.craft_slots.end(),
			                                              [](const auto &slot)
			                                              {
				                                              return slot.role == "transport" &&
				                                                     slot.craft_role == "shuttle";
			                                              }),
			        "supply mission requires a shuttle transport slot");
			d.supply = offer;
		}
		d.retain_craft = j.value("retain_craft", true);
		d.conversation = j.value("conversation", std::string{});
		d.hail_tree = j.value("hail_tree", std::string{});
		require(d.conversation.empty() != d.hail_tree.empty(), "choose conversation or hail_tree");
		d.reveal_event = j.at("reveal_event");
		d.tree = j.at("tree");
		d.allow_ship_transport = j.value("allow_ship_transport", false);
		d.docking_ports = j.value("docking_ports", std::vector<std::string>{});
		require(!d.allow_ship_transport || !d.docking_ports.empty(), "ship transport requires explicit docking_ports");
		d.ship_transport_reason = j.value("ship_transport_reason", std::string{});
		for (const auto *key : {"deployment_range_mm", "deployment_ticks"})
			if (j.contains(key))
				require(j.at(key).is_number_integer() && j.at(key) > 0 &&
				            j.at(key) <= (std::string(key) == "deployment_ticks" ? 36000 : 1000000000),
				        "invalid deployment bounds");
		d.deployment_range = j.value("deployment_range_mm", std::int64_t{20000});
		d.deployment_ticks = j.value("deployment_ticks", std::int64_t{20});
		require(!d.id.empty() && d.id.size() <= 64 &&
		            d.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
		                std::string::npos &&
		            ids.insert(d.id).second,
		        "invalid mission id");
		bool ship_slot = false;
		for (const auto &s : map.value("ship_slots", Json::array()))
			if (s.at("id") == d.slot)
				ship_slot = true;
		require(ship_slot, "unknown ship slot");
		require(catalog.tree_programs.contains(d.tree) &&
		            catalog.tree_programs.at(d.tree).context == state_tree::Context::Away,
		        "expected away tree");
		d.program = catalog.tree_programs.at(d.tree);
		bool target = false, conversation = false;
		std::string dialogue, target_definition;
		if (!d.hail_tree.empty())
		{
			conversation = catalog.conversation_programs.contains(d.hail_tree);
			dialogue = d.hail_tree;
		}
		for (const auto &e : map.value("entities", Json::array()))
			if (e.value("id", EntityId{}) == d.target)
			{
				target = true;
				target_definition = e.at("definition");
			}
		for (const auto &c : map.value("conversations", Json::array()))
			if (c.at("id") == d.conversation)
			{
				conversation = true;
				dialogue = c.at("tree");
			}
		require(target && conversation, "unknown target or conversation");
		if (d.supply)
		{
			require(!d.allow_ship_transport, "supply collection uses its shuttle slot");
			const auto inventory = compile_logistics(catalog, target_definition, directed_catalog, properties);
			const auto &offer = *d.supply;
			const auto provider = std::find_if(inventory.stores.begin(), inventory.stores.end(),
			                                   [&](const auto &s)
			                                   {
				                                   return s.id == offer.provider_store;
			                                   });
			require(provider != inventory.stores.end() &&
			            std::find(provider->accepts.begin(), provider->accepts.end(), offer.material) !=
			                provider->accepts.end() &&
			            (offer.payment.empty() || std::find(provider->accepts.begin(), provider->accepts.end(),
			                                                offer.payment) != provider->accepts.end()),
			        "incompatible supply provider store");
			for (const auto &slot : map.at("ship_slots"))
				if (slot.at("id") == d.slot)
					for (const auto &ship : slot.at("allowed_ships"))
					{
						const auto home =
						    compile_logistics(catalog, ship.get<std::string>(), directed_catalog, properties);
						const auto store = std::find_if(home.stores.begin(), home.stores.end(),
						                                [&](const auto &s)
						                                {
							                                return s.id == offer.home_store;
						                                });
						require(store != home.stores.end() &&
						            std::find(store->accepts.begin(), store->accepts.end(), offer.material) !=
						                store->accepts.end() &&
						            (offer.payment.empty() || std::find(store->accepts.begin(), store->accepts.end(),
						                                                offer.payment) != store->accepts.end()),
						        "incompatible supply home store");
					}
		}
		const auto ports = compile_docking(catalog, target_definition, directed_catalog, properties);
		if (d.supply)
			require(std::any_of(ports.begin(), ports.end(),
			                    [](const auto &port)
			                    {
				                    return port.supply;
			                    }),
			        "supply provider requires a docking supply port");
		std::set<std::string> docking_ports;
		for (const auto &port : d.docking_ports)
		{
			bool found = false;
			for (const auto &p : ports)
				found |= p.id == port;
			require(found && docking_ports.insert(port).second, "unknown/duplicate mission docking port");
		}
		require(!d.reveal_event.empty(), "disclosure line key required");
		const auto &cp = catalog.conversation_programs.at(dialogue);
		bool reveal = false;
		for (const auto &[key, line] : cp.lines)
			if (line.text == d.reveal_event)
				reveal = true;
		require(reveal, "disclosure must name a dialogue line text_key");
		for (const auto &[key, p] : cp.prompts)
			require(p.timeout > 0 && !p.default_choice.empty(), "away decisions require timeout and default");
		if (const auto combat = compile_combat(catalog, target_definition, directed_catalog, properties))
			d.repair_compartments = combat->compartment_ids;
		const auto validate = [&](const auto &self, const state_tree::Program &p) -> void
		{
			for (const auto &s : p.states)
				for (const auto *tasks : {&s.entry, &s.running, &s.exit})
					for (const auto &t : *tasks)
						if (t.kind == state_tree::TaskKind::Action)
						{
							require(t.arguments.size() == 1 && t.arguments[0].source == state_tree::Source::Literal,
							        "away actions require literal arguments");
							if (p.registry.actions.at(t.target).name == "restore_compartment")
								require(d.repair_compartments.contains(t.arguments[0].literal.text),
								        "unknown repair compartment");
							if (p.registry.actions.at(t.target).name == "emit")
							{
								const auto scenario = catalog.scenarios.find(map.at("id").get<std::string>());
								require(scenario != catalog.scenarios.end(), "mission emits without a scenario");
								const auto &events = scenario->second.events;
								require(std::find(events.begin(), events.end(), t.arguments[0].literal.text) !=
								            events.end(),
								        "undeclared mission event");
							}
							if (p.registry.actions.at(t.target).name == "offer")
								require(cp.prompts.contains(t.arguments[0].literal.text), "unknown away prompt");
							if (p.registry.actions.at(t.target).name == "say")
								require(cp.lines.contains(t.arguments[0].literal.text), "unknown away line");
							if (p.registry.actions.at(t.target).name == "require_team" ||
							    p.registry.actions.at(t.target).name == "claim")
								require(std::any_of(p.registry.observations.begin(), p.registry.observations.end(),
								                    [&](const auto &o)
								                    {
									                    return o.name ==
									                           "team_capabilities." + t.arguments[0].literal.text;
								                    }),
								        "undeclared work capability");
							if (p.registry.actions.at(t.target).name == "work" ||
							    p.registry.actions.at(t.target).name == "timed_work")
								require(t.arguments[0].literal.number > 0 && t.arguments[0].literal.number <= 1728000,
								        "invalid work duration");
						}
			for (const auto &ref : p.references)
				self(self, ref);
		};
		validate(validate, d.program);
		std::set<std::string> slots;
		for (const auto &s : j.value("slots", Json::array()))
		{
			AwaySlot v{s.at("id"), s.value("kind", std::string{"team"}), s.value("trait", std::string{}),
			           s.value("required", true)};
			require(!v.id.empty() && v.id.size() <= 64 &&
			            v.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
			                std::string::npos &&
			            slots.insert(v.id).second && (v.kind == "team" || v.kind == "officer"),
			        "invalid slot");
			v.team_type = s.value("team_type", std::string{});
			v.capability = s.value("capability", std::string{});
			v.assign_ai = s.value("assign_ai", false);
			require(v.team_type.size() <= 64 && v.capability.size() <= 64 &&
			            (v.kind == "team" || (v.team_type.empty() && v.capability.empty())),
			        "invalid team slot filter");
			d.slots.push_back(v);
		}
		require((!d.slots.empty() || !d.craft_slots.empty()) && d.slots.size() <= 16,
		        "expected personnel or craft slots");
		out.push_back(std::move(d));
	}
	return out;
}
std::vector<BoardingDefinition> compile_boarding(const Catalog &catalog, const std::string &entity)
{
	const DirectedCatalog directed_catalog{{entity, compile_directed(catalog, entity)}};
	return compile_boarding(catalog, entity, directed_catalog);
}
std::vector<BoardingDefinition> compile_boarding(const Catalog &catalog, const std::string &entity,
                                                 const DirectedCatalog &directed_catalog,
                                                 SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	std::vector<BoardingDefinition> out;
	const auto &hull = catalog.entities.at(entity);
	auto fail = [&](bool ok, const std::string &why)
	{
		if (!ok)
			throw std::runtime_error(entity + ".boarding_missions: " + why);
	};
	const auto rows = hull.value("boarding_missions", Json::array());
	fail(rows.is_array() && rows.size() <= 8, "expected at most 8 templates");
	if (rows.empty())
		return out;
	const auto combat = compile_combat(catalog, entity, directed_catalog, properties);
	fail(combat.has_value(), "boarding requires compartment structure");
	std::set<std::string> ids;
	for (const auto &j : rows)
	{
		BoardingDefinition d;
		d.id = j.at("id");
		d.title = j.at("title_key");
		d.dialogue = j.at("dialogue");
		d.entry = j.at("entry");
		d.retain_craft = j.value("retain_craft", true);
		const auto ports = compile_docking(catalog, entity, directed_catalog, properties);
		std::set<std::string> docking_ports;
		for (const auto &port : j.value("docking_ports", std::vector<std::string>{}))
			fail(docking_ports.insert(port).second && std::any_of(ports.begin(), ports.end(),
			                                                      [&](const auto &p)
			                                                      {
				                                                      return p.id == port;
			                                                      }),
			     "unknown/duplicate boarding docking port");
		fail(!d.id.empty() && d.id.size() <= 24 &&
		         d.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
		             std::string::npos &&
		         ids.insert(d.id).second,
		     "invalid template identity");
		d.compartments = combat->compartment_ids;
		fail(d.compartments.contains(d.entry) && !d.title.empty() && d.title.size() <= 128, "invalid title/entry");
		const auto tree = j.at("tree").get<std::string>();
		fail(catalog.trees.contains(tree) && catalog.trees.at(tree).value("boarding", false),
		     "tree requires boarding: true");
		fail(catalog.tree_programs.contains(tree) &&
		         catalog.tree_programs.at(tree).context == state_tree::Context::Away &&
		         catalog.conversation_programs.contains(d.dialogue),
		     "expected away tree and dialogue");
		d.program = catalog.tree_programs.at(tree);
		for (const auto &[key, prompt] : catalog.conversation_programs.at(d.dialogue).prompts)
			fail(prompt.timeout && !prompt.default_choice.empty(), "boarding prompts require timeouts/defaults");
		std::set<std::string> slots;
		bool security = false;
		for (const auto &s : j.at("slots"))
		{
			AwaySlot slot{s.at("id"), s.value("kind", std::string{"team"}), s.value("trait", std::string{}),
			              s.value("required", true)};
			slot.team_type = s.value("team_type", std::string{});
			slot.capability = s.value("capability", std::string{});
			fail(!slot.id.empty() && slot.id.size() <= 64 &&
			         slot.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
			             std::string::npos &&
			         slots.insert(slot.id).second && (slot.kind == "team" || slot.kind == "officer"),
			     "invalid slot");
			security |= slot.kind == "team" && slot.capability == "security" && slot.required;
			d.slots.push_back(slot);
		}
		fail(security && d.slots.size() <= 16, "required security slot missing or too many slots");
		auto amount = [&](const Json &row, const char *key, std::int64_t fallback, std::int64_t high = 1000)
		{
			const auto v = row.value(key, fallback);
			fail(v >= 0 && v <= high, std::string("invalid ") + key);
			return v;
		};
		for (const auto &e : j.value("encounters", Json::array()))
		{
			BoardingEncounter v;
			v.id = e.at("id");
			v.compartment = e.at("compartment");
			v.report = e.value("report", std::string{});
			fail(d.compartments.contains(v.compartment) && !v.id.empty() && v.id.size() <= 64 &&
			         !d.encounters.contains(v.id),
			     "invalid encounter");
			v.defenders = e.value("defenders", std::vector<std::string>{});
			fail(v.defenders.size() <= 128 &&
			         std::set<std::string>(v.defenders.begin(), v.defenders.end()).size() == v.defenders.size(),
			     "duplicate or excessive defending teams");
			for (const auto &id : v.defenders)
				fail(combat->system_ids.contains(id) &&
				         combat->systems.at(combat->system_ids.at(id)).kind == OperatingSystem::Team &&
				         combat->systems.at(combat->system_ids.at(id))
				             .get<TeamDefinition>()
				             .capabilities.contains("security"),
				     "defender requires a security team");
			v.defences = e.value("defences", std::map<std::string, std::int64_t>{});
			fail(v.defences.size() <= 32, "too many defence factors");
			for (const auto &[id, penalty] : v.defences)
				fail(combat->equipment_compartments.contains(id) && penalty >= 0 && penalty <= 1000,
				     "invalid defence System/factor");
			v.success = amount(e, "success_permille", 700);
			v.team_bonus = amount(e, "team_bonus_permille", 100);
			v.defender_penalty = amount(e, "defender_penalty_permille", 150);
			v.injury = amount(e, "injury_permille", 250);
			v.loss = amount(e, "loss_permille", 50);
			v.medical_reduction = amount(e, "medical_reduction_permille", 150);
			v.assessment_ticks = amount(e, "assessment_ticks", 20, 1728000);
			v.officer_injury = amount(e, "officer_injury_permille", 0);
			v.officer_loss = amount(e, "officer_loss_permille", 0);
			fail(v.injury + v.loss <= 1000 && v.officer_injury + v.officer_loss <= 1000,
			     "casualty probabilities exceed 1000");
			fail(v.report.empty() || catalog.conversation_programs.at(d.dialogue).lines.contains(v.report),
			     "unknown assessment report");
			d.encounters.emplace(v.id, v);
		}
		for (const auto &[key, o] : j.at("objectives").items())
		{
			BoardingObjective v{o.at("compartment"),
			                    o.at("kind"),
			                    o.value("system", std::string{}),
			                    o.value("fault", std::string{}),
			                    o.value("prize_crew", true),
			                    o.value("prize_capability", std::string{})};
			fail(d.compartments.contains(v.compartment) &&
			         (v.kind == "bridge" || v.kind == "hold" || v.kind == "sabotage"),
			     "invalid objective");
			if (v.kind != "bridge")
				fail(combat->equipment_compartments.contains(v.system) &&
				         combat->equipment_compartments.at(v.system) == v.compartment,
				     "objective System is outside its compartment");
			if (v.kind == "sabotage")
			{
				const auto f = combat->faults.find(v.system);
				fail(f != combat->faults.end() && std::any_of(f->second.begin(), f->second.end(),
				                                              [&](const auto &f)
				                                              {
					                                              return f.id == v.fault && !f.destroys;
				                                              }),
				     "sabotage requires a repairable authored fault");
			}
			d.objectives.emplace(key, v);
		}
		fail(d.encounters.size() <= 32 && d.objectives.size() <= 32, "too many encounters/objectives");
		const auto validate = [&](const auto &self, const state_tree::Program &p) -> void
		{
			for (const auto &state : p.states)
				for (const auto *tasks : {&state.entry, &state.running, &state.exit})
					for (const auto &t : *tasks)
						if (t.kind == state_tree::TaskKind::Action)
						{
							const auto &name = p.registry.actions.at(t.target).name;
							fail(t.arguments.size() == 1 && t.arguments[0].source == state_tree::Source::Literal,
							     "actions require literal arguments");
							const auto &id = t.arguments[0].literal.text;
							if (name == "move_to" || name == "restore_compartment")
								fail(d.compartments.contains(id), "unknown compartment");
							if (name == "encounter")
								fail(d.encounters.contains(id), "unknown encounter");
							if (name == "secure" || name == "release" || name == "capture")
								fail(d.objectives.contains(id), "unknown objective");
							if (name == "capture")
								fail(d.objectives.at(id).kind == "bridge", "capture requires bridge objective");
							fail(name != "emit", "boarding uses mission outcomes rather than map-specific events");
						}
			for (const auto &ref : p.references)
				self(self, ref);
		};
		validate(validate, d.program);
		out.push_back(std::move(d));
	}
	return out;
}
} // namespace phoenix
