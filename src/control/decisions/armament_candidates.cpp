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
import phoenix.knowledge.decision_combat;
import phoenix.control.candidate_facts;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<CandidateFacts> controller_armament_candidates(const ConsoleControllerRuntime &controller,
                                                           const ControllerObservationSources &input)
{
	using state_tree::Value;
	using Facts = CandidateFacts;
	const auto contacts = controller_contact_candidates(controller, input);
	std::set<std::string> owned;
	for (const auto &[role, systems] : controller.definition.equipment)
		owned.insert(systems.begin(), systems.end());
	std::vector<Facts> result;
	for (const auto &weapon : input.observation.combat.weapons)
	{
		const auto id = weapon.id;
		if (!owned.contains(id))
			continue;
		std::int64_t accepted_target = 0;
		if (const auto at = input.observation.combat.directed_targets.find(id);
		    at != input.observation.combat.directed_targets.end())
			accepted_target = at->second;
		for (const auto &contact : contacts)
			for (const auto &option : weapon.ammunition_options)
			{
				Facts facts = contact;
				const auto integer = [&](CandidateField name, std::int64_t value)
				{
					facts[name] = Value::integer(value);
				};
				const auto boolean = [&](CandidateField name, bool value)
				{
					facts[name] = Value::boolean(value);
				};
				const auto text = [&](CandidateField name, const std::string &value)
				{
					facts[name] = Value::string(value);
				};
				const auto index = option.index;
				const auto range = option.range_mm;
				const bool available = option.available;
				const bool enabled = weapon.enabled;
				const bool current = contact.at(CandidateField::contact_candidate_current).number != 0;
				const bool hostile = contact.at(CandidateField::contact_candidate_hostile).number != 0;
				const bool in_range = contact.at(CandidateField::contact_candidate_distance).number <= range;
				text(CandidateField::armament_weapon, id);
				text(CandidateField::armament_magazine, option.id);
				text(CandidateField::armament_guidance, option.guidance);
				text(CandidateField::armament_reason, !enabled     ? "weapon_unavailable"
				                                      : !available ? "ammunition_unavailable"
				                                      : !current   ? "current_targeting_unavailable"
				                                      : !hostile   ? "no_known_hostile"
				                                      : !in_range  ? "target_outside_payload_range"
				                                                   : "permitted_armament_option");
				integer(CandidateField::armament_index, index);
				integer(CandidateField::armament_target, accepted_target);
				integer(CandidateField::armament_range, range);
				integer(CandidateField::armament_quantity, option.quantity);
				integer(CandidateField::armament_use, option.use);
				integer(CandidateField::armament_retask_cost, weapon.selected == index ? 0 : 1);
				boolean(CandidateField::armament_available, available);
				boolean(CandidateField::armament_in_range, in_range);
				boolean(CandidateField::armament_selected, weapon.selected == index);
				boolean(CandidateField::armament_loaded, weapon.loaded == index);
				boolean(CandidateField::armament_enabled, enabled);
				boolean(CandidateField::armament_fire_allowed, true);
				result.push_back(std::move(facts));
			}
	}
	if (result.empty())
	{
		auto empty = empty_candidate_facts(controller, CandidateGroup::contact_candidate);
		for (const auto &[key, value] : empty_candidate_facts(controller, CandidateGroup::armament))
			empty[key] = value;
		empty[CandidateField::armament_reason] = Value::string("no_owned_ammunition_options");
		result.push_back(std::move(empty));
	}
	return result;
}
CandidateAdapter armament_adapter()
{
	return {"armament",
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
		        if ((context.source() == CandidateSource::contacts || context.source() == CandidateSource::armament) &&
		            (!context.text(*context.record, ObjectiveField::mission, "").empty() ||
		             !context.text(*context.record, ObjectiveField::compartment, "").empty() ||
		             (!context.text(*context.record, ObjectiveField::system, "").empty() &&
		              (context.source() != CandidateSource::armament ||
		               context.text(*context.record, ObjectiveField::system, "") !=
		                   context.fact(CandidateField::armament_weapon).text)) ||
		             (context.number(*context.record, ObjectiveField::target) &&
		              context.number(*context.record, ObjectiveField::target) !=
		                  context.fact(CandidateField::contact_candidate_id).number)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if ((context.source() == CandidateSource::contacts || context.source() == CandidateSource::armament) &&
		            candidate.reason == "binding_not_applicable")
		        {
			        if (context.source() == CandidateSource::armament &&
			            context.fact(CandidateField::armament_weapon).text.empty())
				        candidate.reason = "no_owned_ammunition_options";
			        else if (!context.fact(CandidateField::contact_candidate_available).number)
				        candidate.reason = "no_permitted_contact";
			        else if (context.behavior == "engage" &&
			                 !context.fact(CandidateField::contact_candidate_hostile).number &&
			                 !(context.fact(CandidateField::contact_candidate_category).text == "missile" &&
			                   context.fact(CandidateField::contact_candidate_disposition).text != "friendly"))
				        candidate.reason = "no_known_hostile";
		        }
		        if (context.source() == CandidateSource::contacts || context.source() == CandidateSource::armament)
			        candidate.target = context.fact(CandidateField::contact_candidate_id).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::armament &&
		            context.fact(CandidateField::armament_reason).text != "permitted_armament_option")
			        candidate.reason = context.fact(CandidateField::armament_reason).text;
	        },
	        [](CandidateContext &context) -> Json
	        {
		        return {};
	        }};
}
} // namespace phoenix
