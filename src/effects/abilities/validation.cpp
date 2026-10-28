module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.effects.ability_validation;

namespace phoenix
{
void validate_ability_runtime(EntityId ship, std::uint64_t tick, const AbilityRuntime &state,
                              const AbilityRuntime &initial, const AbilityDefinitionSet &abilities,
                              const std::map<std::string, AbilityGrantDefinition> &grant_definitions,
                              const AbilityValidationPorts &ports, std::int64_t coordinate_unit)
{
	constexpr auto maximum = 9007199254740991ULL;
	if (!state.next_generation || state.next_generation > ability_generation_limit || !state.next_activation ||
	    state.next_activation > maximum || state.grants.size() > ability_grant_limit ||
	    state.recovery.size() > ability_recovery_limit)
		throw std::runtime_error("Invalid saved ability roster bounds");
	if (state.work_tick > tick || state.work.size() > ability_grant_limit)
		throw std::runtime_error("Invalid saved ability work receipt bounds");
	std::set<std::uint64_t> work_activations;
	for (const auto &receipt : state.work)
	{
		const auto &catalogue = abilities.catalog->definitions;
		const auto found = catalogue.find(receipt.definition);
		if (found == catalogue.end() || found->second.family != receipt.family || !receipt.activation ||
		    receipt.activation >= state.next_activation || !work_activations.insert(receipt.activation).second ||
		    receipt.output <= 0 || receipt.output > 1000 || receipt.work != receipt.output ||
		    receipt.work_unit != "permille" || receipt.execution.empty() || receipt.execution.size() > 1024 ||
		    !receipt.generation || receipt.generation >= state.next_generation ||
		    !ports.source_exists(receipt.source, receipt.owner))
			throw std::runtime_error("Invalid saved ability completed work");
	}
	const auto &definitions = abilities.catalog->definitions;
	std::set<std::uint64_t> generations, activations;
	std::set<std::string> exclusions, running_sources;
	for (const auto &[key, deadline] : state.recovery)
	{
		// Source and definition identifiers cannot contain ':'. Inspect actual
		// sparse ledger entries rather than manufacturing every possible key.
		const auto first = key.find(':'), second = first == std::string::npos ? first : key.find(':', first + 1);
		if (key.size() > 1024 || first == std::string::npos || second == std::string::npos || first == 0 ||
		    second == first + 1 || second + 1 == key.size() || key.find(':', second + 1) != std::string::npos)
			throw std::runtime_error("Invalid saved ability recovery identity");
		const auto source = key.substr(0, first), owner = key.substr(first + 1, second - first - 1),
		           reference = key.substr(second + 1);
		std::int64_t bound = 0;
		if (source == "apparatus")
		{
			const auto authored = abilities.apparatus_recovery_bounds.find(key);
			if (!ports.source_exists("equipment", owner) || authored == abilities.apparatus_recovery_bounds.end())
				throw std::runtime_error("Invalid saved apparatus recovery binding");
			bound = authored->second;
		}
		else
		{
			const auto ability = definitions.find(reference);
			if (!ports.source_exists(source, owner) || ability == definitions.end())
				throw std::runtime_error("Invalid saved ability recovery source");
			bound = std::max(ability->second.recovery_ticks, ability->second.interruption_recovery_ticks);
		}
		if (!bound || deadline > maximum || deadline > tick + bound)
			throw std::runtime_error("Invalid saved ability recovery owner/deadline");
	}
	for (const auto &[id, grant] : state.grants)
	{
		const auto found = definitions.find(grant.definition);
		if (id != grant.id || id.empty() || id.size() > 1024 || found == definitions.end() ||
		    !ports.source_exists(grant.source, grant.owner) || !grant.generation ||
		    grant.generation >= state.next_generation || !generations.insert(grant.generation).second ||
		    grant.activation >= state.next_activation || grant.request_id > maximum || grant.expiry < -1 ||
		    (grant.expiry >= 0 && (grant.expiry < static_cast<std::int64_t>(tick) ||
		                           grant.expiry > static_cast<std::int64_t>(tick) + 1000000)) ||
		    grant.parent.size() > 512 || grant.control.size() > 256 || grant.apparatus.size() > 256 ||
		    grant.aim_x < -coordinate_unit || grant.aim_x > coordinate_unit || grant.aim_y < -coordinate_unit ||
		    grant.aim_y > coordinate_unit)
			throw std::runtime_error("Invalid saved ability grant");
		if (grant.funding_permille < 0 || grant.funding_permille > 1000 || grant.credits.size() > 4096)
			throw std::runtime_error("Invalid saved ability payment");
		std::set<std::tuple<EntityId, std::string, std::string, std::string, std::string, std::string, std::int64_t>>
		    credit_keys;
		for (const auto &credit : grant.credits)
		{
			ports.credit(credit);
			if (!credit_keys
			         .emplace(credit.payer, credit.store, credit.resource, credit.property, credit.equipment,
			                  credit.officer, credit.compartment)
			         .second)
				throw std::runtime_error("Duplicate saved ability work credit");
		}
		const auto &ability = found->second;
		if (grant.retained_request && (!ability.sustained || grant.phase != "ready" || grant.control.empty()))
			throw std::runtime_error("Invalid retained ability request");

		if (grant.phase != "ready" && grant.phase != "preparing" && grant.phase != "active" && grant.phase != "paused")
			throw std::runtime_error("Invalid saved ability phase");
		const bool activated = grant.phase == "active" || (grant.phase == "paused" && grant.resume_phase == "active");
		if (grant.progress < 0 || grant.progress > ability.preparation_ticks * 1000 ||
		    (grant.phase == "paused" ? grant.resume_phase != "preparing" && grant.resume_phase != "active"
		                             : !grant.resume_phase.empty()) ||
		    (activated && (!ability.sustained || grant.progress != ability.preparation_ticks * 1000)))
			throw std::runtime_error("Contradictory saved ability preparation");
		if (grant.parent.empty())
		{
			const auto expected = initial.grants.find(id);
			if (expected == initial.grants.end() || !expected->second.parent.empty() ||
			    expected->second.definition != grant.definition || expected->second.source != grant.source ||
			    expected->second.owner != grant.owner || grant.expiry != -1 || !grant.grant_effect.empty())
				throw std::runtime_error("Saved static ability does not match its source grant");
		}
		else
		{
			const auto effect = grant_definitions.find(grant.grant_effect);
			if (effect == grant_definitions.end() || effect->second.ability != grant.definition ||
			    id != "temporary:" + grant.parent + ":" + grant.source + ":" + grant.owner ||
			    (effect->second.duration_ticks == 0
			         ? grant.expiry != -1
			         : grant.expiry < 0 ||
			               grant.expiry > static_cast<std::int64_t>(tick) + effect->second.duration_ticks))
				throw std::runtime_error("Invalid saved temporary ability lineage/lifetime");
		}
		const auto *control = ports.control(grant);
		if (!grant.control.empty() && (!control || control->id != grant.control))
			throw std::runtime_error("Saved ability control binding is stale");
		if (grant.retained_request)
		{
			if (!control)
				throw std::runtime_error("Retained ability request has no control");
			const auto intent = ports.intent(*control);
			if (!intent.request_semantics || !intent.value)
				throw std::runtime_error("Retained ability request disagrees with accepted intent");
		}
		if (grant.phase != "ready")
		{
			if (!running_sources.insert(ability_recovery_key(grant)).second)
				throw std::runtime_error("Duplicate saved running source ability");
			if ((ability.recovery_ticks || ability.interruption_recovery_ticks) &&
			    !state.recovery.contains(ability_recovery_key(grant)))
				throw std::runtime_error("Missing saved source recovery reservation");
			if (ability.apparatus_recovery_ticks &&
			    (grant.apparatus.empty() ||
			     !state.recovery.contains("apparatus:" + grant.apparatus + ":" + ability.apparatus_recovery_key)))
				throw std::runtime_error("Missing saved apparatus recovery reservation");
			if (!control || grant.control.empty() ||
			    (activated && (!grant.activation || !activations.insert(grant.activation).second)) ||
			    (ability.target == "self" && grant.target != ship) ||
			    (ability.target != "self" && (!grant.target || !ports.target_exists(grant.target))))
				throw std::runtime_error("Invalid saved active ability context");
			const auto apparatus =
			    control->apparatus.empty() && grant.source == "equipment" ? grant.owner : control->apparatus;
			if (grant.apparatus != apparatus)
				throw std::runtime_error("Saved ability apparatus binding is stale");
			const auto intent = ports.intent(*control);
			if ((!ability.sustained && (!grant.request_id || intent.request_id != grant.request_id ||
			                            intent.value != 1 || !intent.accepted)) ||
			    (!intent.request_semantics && grant.request_id) ||
			    (ability.sustained && grant.request_id && intent.request_id == grant.request_id &&
			     (intent.value != 1 || !intent.accepted)))
				throw std::runtime_error("Saved ability execution request is inconsistent");
			for (const auto &exclusion : ability.exclusions)
				if (!exclusions.insert(ability_exclusion_key(grant, exclusion)).second)
					throw std::runtime_error("Conflicting saved ability exclusions");
		}
	}
	for (const auto &[id, grant] : initial.grants)
		if (grant.parent.empty() && !state.grants.contains(id))
			throw std::runtime_error("Missing saved static ability grant");
}

} // namespace phoenix
