module;
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
state_tree::Registry away_registry()
{
	using namespace state_tree;
	Registry r;
	for (const auto &name : {"deployed", "recovered", "work_done"})
		r.observations.push_back({name, Type::Boolean, 16, {}});
	for (const auto &name : {"tick", "progress"})
		r.observations.push_back({name, Type::Integer, 16, {}});
	for (const auto &name : {"answer", "checkpoint"})
		r.observations.push_back({name, Type::Text, 16, {}});
	for (const auto &name : {"report", "checkpoint", "emit", "complete", "fail", "restore_compartment"})
		r.actions.push_back({name, 16, {Type::Text}, true});
	r.actions.push_back({"work", 16, {Type::Integer}, true});
	return r;
}
std::vector<AwayDefinition> compile_away(const Catalog &catalog, const Json &map)
{
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
		d.target = j.at("target_entity");
		d.conversation = j.at("conversation");
		d.reveal_event = j.at("reveal_event");
		d.tree = j.at("tree");
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
		require(!d.reveal_event.empty(), "disclosure line key required");
		const auto &cp = catalog.conversation_programs.at(dialogue);
		bool reveal = false;
		for (const auto &[key, line] : cp.lines)
			if (line.text == d.reveal_event)
				reveal = true;
		require(reveal, "disclosure must name a dialogue line text_key");
		for (const auto &[key, p] : cp.prompts)
			require(p.timeout > 0 && !p.default_choice.empty(), "away decisions require timeout and default");
		if (const auto combat = compile_combat(catalog, target_definition))
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
							if (p.registry.actions.at(t.target).name == "work")
								require(t.arguments[0].literal.number > 0 && t.arguments[0].literal.number <= 1728000,
								        "invalid work duration");
						}
			for (const auto &ref : p.references)
				self(self, ref);
		};
		validate(validate, d.program);
		std::set<std::string> slots;
		for (const auto &s : j.at("slots"))
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
			require(v.team_type.size() <= 64 && v.capability.size() <= 64 &&
			            (v.kind == "team" || (v.team_type.empty() && v.capability.empty())),
			        "invalid team slot filter");
			d.slots.push_back(v);
		}
		require(!d.slots.empty() && d.slots.size() <= 16, "expected 1..16 slots");
		out.push_back(std::move(d));
	}
	return out;
}
} // namespace phoenix
