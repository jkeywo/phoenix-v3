module;
#include <optional>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <span>
#include <map>
#include <set>
#include <string>
#include <vector>

module phoenix.control.decisions;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<std::map<std::string, state_tree::Value>>
controller_armament_candidates(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input)
{
	using state_tree::Value;
	using Facts = std::map<std::string, Value>;
	const auto contacts = controller_contact_candidates(controller, input);
	std::set<std::string> owned;
	for (const auto &[role, systems] : controller.definition.equipment)
		owned.insert(systems.begin(), systems.end());
	std::vector<Facts> result;
	for (const auto &weapon : combat_observation(input.observation).at("weapons"))
	{
		const auto id = weapon.at("id").get<std::string>();
		if (!owned.contains(id))
			continue;
		std::int64_t accepted_target = 0;
		if (const auto orders = input.observation.gameplay.find("directed"); orders != input.observation.gameplay.end())
			for (const auto &order : *orders)
				if (order.at("id") == id)
					accepted_target = order.at("target");
		for (const auto &contact : contacts)
			for (const auto &option : weapon.at("ammunition_options"))
			{
				Facts facts = contact;
				const auto integer = [&](const char *name, std::int64_t value)
				{
					facts[std::string("armament.") + name] = Value::integer(value);
				};
				const auto boolean = [&](const char *name, bool value)
				{
					facts[std::string("armament.") + name] = Value::boolean(value);
				};
				const auto text = [&](const char *name, const std::string &value)
				{
					facts[std::string("armament.") + name] = Value::string(value);
				};
				const auto index = option.at("index").get<std::int64_t>();
				const auto range = option.at("range_mm").get<std::int64_t>();
				const bool available = option.at("available").get<bool>();
				const bool enabled = weapon.at("enabled").get<bool>();
				const bool current = contact.at("contact_candidate.current").number != 0;
				const bool hostile = contact.at("contact_candidate.hostile").number != 0;
				const bool in_range = contact.at("contact_candidate.distance").number <= range;
				text("weapon", id);
				text("magazine", option.at("id"));
				text("guidance", option.value("guidance", std::string{}));
				text("reason", !enabled     ? "weapon_unavailable"
				               : !available ? "ammunition_unavailable"
				               : !current   ? "current_targeting_unavailable"
				               : !hostile   ? "no_known_hostile"
				               : !in_range  ? "target_outside_payload_range"
				                            : "permitted_armament_option");
				integer("index", index);
				integer("target", accepted_target);
				integer("range", range);
				integer("quantity", option.value("quantity", std::int64_t{0}));
				integer("use", option.value("use", std::int64_t{0}));
				integer("retask_cost", weapon.value("selected", -1LL) == index ? 0 : 1);
				boolean("available", available);
				boolean("in_range", in_range);
				boolean("selected", weapon.value("selected", -1LL) == index);
				boolean("loaded", weapon.value("loaded", -1LL) == index);
				boolean("enabled", enabled);
				boolean("fire_allowed", true);
				result.push_back(std::move(facts));
			}
	}
	if (result.empty())
	{
		Facts empty;
		for (const auto &field : controller.brain->program.registry.observations)
			if (field.name.starts_with("contact_candidate.") || field.name.starts_with("armament."))
				empty[field.name] = field.type == state_tree::Type::Text      ? Value::string("")
				                    : field.type == state_tree::Type::Boolean ? Value::boolean(false)
				                                                              : Value::integer(0);
		empty["armament.reason"] = Value::string("no_owned_ammunition_options");
		result.push_back(std::move(empty));
	}
	return result;
}
CandidateAdapter armament_adapter()
{
	return {
	    "armament",
	    "objective_parameter_outside_known_contact_scope",
	    false,
	    [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	       const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	       const KnowledgeRecord &record,
	       std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	    {
		    return candidate_frames(cache.armament_options(), controller, source);
	    },
	    [](CandidateContext &context)
	    {
		    if ((context.binding.candidate_source == "contacts" || context.binding.candidate_source == "armament") &&
		        (!context.text(*context.record, "mission", "").empty() ||
		         !context.text(*context.record, "compartment", "").empty() ||
		         (!context.text(*context.record, "system", "").empty() &&
		          (context.binding.candidate_source != "armament" ||
		           context.text(*context.record, "system", "") != context.fact("armament.weapon").text)) ||
		         (context.number(*context.record, "target") &&
		          context.number(*context.record, "target") != context.fact("contact_candidate.id").number)))
			    return false;
		    return true;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if ((context.binding.candidate_source == "contacts" || context.binding.candidate_source == "armament") &&
		        candidate.reason == "binding_not_applicable")
		    {
			    if (context.binding.candidate_source == "armament" && context.fact("armament.weapon").text.empty())
				    candidate.reason = "no_owned_ammunition_options";
			    else if (!context.fact("contact_candidate.available").number)
				    candidate.reason = "no_permitted_contact";
			    else if (context.behavior == "engage" && !context.fact("contact_candidate.hostile").number &&
			             !(context.fact("contact_candidate.category").text == "missile" &&
			               context.fact("contact_candidate.disposition").text != "friendly"))
				    candidate.reason = "no_known_hostile";
		    }
		    if (context.binding.candidate_source == "contacts" || context.binding.candidate_source == "armament")
			    candidate.target = context.fact("contact_candidate.id").number;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if (context.binding.candidate_source == "armament" &&
		        context.fact("armament.reason").text != "permitted_armament_option")
			    candidate.reason = context.fact("armament.reason").text;
	    },
	    [](CandidateContext &context) -> Json
	    {
		    return {};
	    }};
}
} // namespace phoenix
