module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>
module phoenix.content;
namespace phoenix
{
state_tree::Registry interdiction_registry()
{
	using namespace state_tree;
	Registry r;
	for (const auto *name :
	     {"started", "attacked", "reinforced", "captured", "target_lost", "player_claimed", "takeover_ready"})
		r.observations.push_back({name, Type::Boolean, 2, {}});
	r.observations.push_back({"answer", Type::Text, 2, {}});
	for (const auto *name : {"spawn", "posture", "say", "offer", "takeover", "log", "emit", "finish"})
		r.actions.push_back({name, 2, {Type::Text}, true});
	return r;
}
std::vector<InterdictionDefinition> compile_interdictions(const Catalog &catalog, const Json &map)
{
	const auto directed_catalog = compile_directed_catalog(catalog);
	return compile_interdictions(catalog, map, directed_catalog);
}
std::vector<InterdictionDefinition> compile_interdictions(const Catalog &catalog, const Json &map,
                                                          const DirectedCatalog &directed_catalog)
{
	std::vector<InterdictionDefinition> result;
	std::set<std::string> ids;
	if (!map.value("interdictions", Json::array()).is_array())
		throw std::runtime_error("Interdictions must be an array");
	for (const auto &j : map.value("interdictions", Json::array()))
	{
		InterdictionDefinition d;
		d.id = j.at("id");
		d.slot = j.at("player_slot");
		d.target = j.at("target_entity");
		d.start_event = j.at("start_event");
		d.tree = j.at("tree");
		d.dialogue = j.at("dialogue");
		d.target_character = j.value("target_character", std::string{});
		d.threat = j.at("threat_radius_mm");
		d.clearance = j.at("clearance_radius_mm");
		d.engagement = j.at("engagement_radius_mm");
		d.formation = j.at("formation_radius_mm");
		d.takeover_ticks = j.at("takeover_ticks");
		d.combat_standoff = j.value("combat_standoff_mm", 10000LL);
		d.cruise_speed = j.value("cruise_speed_mm_s", 20000LL);
		d.occupation_teams = j.value("occupation_teams", std::vector<std::string>{});
		if (!catalog.conversation_programs.contains(d.dialogue))
			throw std::runtime_error("Unknown interdiction dialogue");
		std::set<std::int64_t> plea_ticks;
		for (const auto &plea : j.value("pleas", Json::array()))
		{
			const auto tick = plea.at("ticks").get<std::int64_t>();
			const auto line = plea.at("line").get<std::string>();
			if (tick <= 0 || tick >= d.takeover_ticks || d.pleas.size() >= 8 || !plea_ticks.insert(tick).second ||
			    !catalog.conversation_programs.at(d.dialogue).lines.contains(line))
				throw std::runtime_error("Invalid interdiction plea");
			d.pleas.emplace_back(tick, line);
		}
		if (result.size() >= 8 || !ids.insert(d.id).second || d.id.empty() || d.id.size() > 64 || d.threat <= 0 ||
		    d.clearance < d.threat || d.engagement < d.clearance || d.engagement > 1000000000 || d.formation <= 0 ||
		    d.formation > 1000000000 || d.takeover_ticks <= 0 || d.takeover_ticks > 1728000 || d.combat_standoff <= 0 ||
		    d.combat_standoff > d.engagement || d.cruise_speed <= 0 || d.cruise_speed > 1000000 ||
		    !catalog.conversation_programs.contains(d.dialogue) || !catalog.tree_programs.contains(d.tree))
			throw std::runtime_error("Invalid interdiction definition");
		bool slot = false, target = false;
		std::string target_hull;
		for (const auto &s : map.at("ship_slots"))
			slot |= s.at("id") == d.slot;
		for (const auto &e : map.at("entities"))
			if (e.at("id") == d.target)
			{
				target = true;
				target_hull = e.at("definition");
			}
		if (!slot || !target)
			throw std::runtime_error("Invalid interdiction actors");
		d.program = catalog.tree_programs.at(d.tree);
		if (d.program.context != state_tree::Context::Scenario ||
		    !state_tree::resolve_definition(catalog.trees.at(d.tree), catalog.trees).value("interdiction", false))
			throw std::runtime_error("Expected an interdiction scenario tree");
		const auto declared_event = [&](const std::string &event)
		{
			const auto map_id = map.at("id").get<std::string>();
			if (!catalog.scenarios.contains(map_id))
				return false;
			const auto &events = catalog.scenarios.at(map_id).events;
			return std::find(events.begin(), events.end(), event) != events.end();
		};
		if (!d.start_event.empty() && !declared_event(d.start_event))
			throw std::runtime_error("Undeclared interdiction start event");
		if (compile_docking(catalog, target_hull, directed_catalog).empty() ||
		    compile_ftl(catalog, target_hull).empty())
			throw std::runtime_error("Interdiction target requires docking and FTL");
		const auto target_systems = resolve_systems(catalog, target_hull);
		std::set<std::string> occupation;
		for (const auto &team : d.occupation_teams)
			if (occupation.size() >= 16 || !occupation.insert(team).second ||
			    std::none_of(target_systems.begin(), target_systems.end(),
			                 [&](const auto &s)
			                 {
				                 return s.id == team && s.kind == OperatingSystem::Team;
			                 }))
				throw std::runtime_error("Invalid occupation team");
		if (!j.at("variants").is_object() || j.at("variants").size() > 32)
			throw std::runtime_error("Invalid interdiction variants");
		for (auto v = j.at("variants").begin(); v != j.at("variants").end(); ++v)
		{
			if (v.key() != "default" && !catalog.entities.contains(v.key()))
				throw std::runtime_error("Unknown interdiction fit");
			if (!v.value().is_array() || v.value().empty())
				throw std::runtime_error("Empty interdiction fleet");
			for (const auto &p : v.value())
			{
				const auto definition = p.at("definition").get<std::string>();
				if (!catalog.entities.contains(definition) || d.variants[v.key()].size() >= 8)
					throw std::runtime_error("Invalid interdiction ship");
				const auto bearing =
				    p.value("formation_mdeg", static_cast<std::int64_t>(d.variants[v.key()].size()) * 120000);
				d.variants[v.key()].push_back(
				    {definition, p.at("x_mm"), p.at("y_mm"), p.value("heading_mdeg", 0LL), bearing});
				const auto &spawn = d.variants[v.key()].back();
				if (spawn.x < -1000000000 || spawn.x > 1000000000 || spawn.y < -1000000000 || spawn.y > 1000000000 ||
				    spawn.heading < 0 || spawn.heading >= 360000 || spawn.formation_bearing < 0 ||
				    spawn.formation_bearing >= 360000 ||
				    compile_docking(catalog, definition, directed_catalog).empty() ||
				    compile_ftl(catalog, definition).empty())
					throw std::runtime_error("Interdiction ship requires valid transform, docking and FTL");
				const auto fits = resolve_systems(catalog, definition);
				for (auto kind : {OperatingSystem::Engine, OperatingSystem::Steering, OperatingSystem::Comms})
					if (std::none_of(fits.begin(), fits.end(),
					                 [&](const auto &s)
					                 {
						                 return s.kind == kind;
					                 }))
						throw std::runtime_error("Interdiction ship missing required equipment");
			}
		}
		if (!d.variants.contains("default") || d.variants.at("default").empty())
			throw std::runtime_error("Interdiction needs default ships");
		const auto validate = [&](const auto &self, const state_tree::Program &program) -> void
		{
			for (const auto &state : program.states)
				for (const auto *tasks : {&state.entry, &state.running, &state.exit})
					for (const auto &task : *tasks)
						if (task.kind == state_tree::TaskKind::Action)
						{
							if (task.arguments.at(0).source != state_tree::Source::Literal)
								throw std::runtime_error("Interdiction action references must be literal");
							const auto ref = task.arguments[0].literal.text;
							const auto action = program.registry.actions.at(task.target).name;
							const auto &dialogue = catalog.conversation_programs.at(d.dialogue);
							if ((action == "posture" && ref != "watch" && ref != "truce" && ref != "hostile") ||
							    (action == "say" && !dialogue.lines.contains(ref)) ||
							    (action == "offer" && !dialogue.prompts.contains(ref)) ||
							    (action == "emit" && !declared_event(ref)) || ref.size() > 512)
								throw std::runtime_error("Invalid interdiction action reference");
						}
			for (const auto &child : program.references)
				self(self, child);
		};
		validate(validate, d.program);
		result.push_back(std::move(d));
	}
	return result;
}
} // namespace phoenix
