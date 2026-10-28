module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

module phoenix.effects.transactions;

namespace phoenix
{
bool same_effect_properties(const PropertyEffects &a, const PropertyEffects &b)
{
	return a == b;
}
bool same_effect_shields(const ShieldComponent &a, const ShieldComponent &b)
{
	if (a.next_effect != b.next_effect || a.contributions.size() != b.contributions.size())
		return false;
	for (std::size_t i = 0; i < a.contributions.size(); ++i)
	{
		const auto &x = a.contributions[i];
		const auto &y = b.contributions[i];
		// Definitions are immutable during a running game. Grant identities bind
		// their copied definition; native work changes only the fields below.
		if (std::tie(x.effect_id, x.source, x.equipment, x.definition_id, x.expiry, x.operating, x.quoted_rate,
		             x.quoted_sustain, x.quoted_discharge, x.protection, x.state.strength, x.state.warmup,
		             x.state.recharge_remainder, x.state.discharge_remainder, x.state.discharge_credit,
		             x.state.discharge) != std::tie(y.effect_id, y.source, y.equipment, y.definition_id, y.expiry,
		                                            y.operating, y.quoted_rate, y.quoted_sustain, y.quoted_discharge,
		                                            y.protection, y.state.strength, y.state.warmup,
		                                            y.state.recharge_remainder, y.state.discharge_remainder,
		                                            y.state.discharge_credit, y.state.discharge))
			return false;
	}
	return true;
}
bool same_effect_abilities(const AbilityRuntime &a, const AbilityRuntime &b)
{
	return a == b;
}
bool same_effect_active_rules(const std::vector<ActiveRuleEffect> &a, const std::vector<ActiveRuleEffect> &b)
{
	return a == b;
}

bool effect_payment_stale(const PreparedEffectPayment &payment, const ResourceNetwork &current)
{
	if (current.revision() != payment.revision || current.stores().size() != payment.stores.size() ||
	    current.coolant_sources().size() != payment.coolant.size())
		return true;
	for (std::size_t i = 0; i < payment.stores.size(); ++i)
		if (current.stores()[i].stock.revision() != payment.stores[i])
			return true;
	for (std::size_t i = 0; i < payment.coolant.size(); ++i)
		if (current.coolant_sources()[i].stock.revision() != payment.coolant[i])
			return true;
	return false;
}

NativeRuleContext effect_invocation_context(const EffectGroupInvocation &frame, std::int64_t tick)
{
	if (frame.rule_context)
		return *frame.rule_context;
	NativeRuleContext context;
	context.owner = context.subject = context.target = {frame.protected_ship};
	context.source =
	    context.instigator = {frame.attacker ? frame.attacker : frame.protected_ship, frame.original_equipment};
	context.hit = frame.hit_context.value_or(rule_hit_context(context.subject));
	context.tick = tick;
	return context;
}

RuleActor effect_group_actor(const EffectGroupInvocation &frame, const std::string &role)
{
	if (frame.rule_context)
		return rule_actor(*frame.rule_context, role);
	if (role == "protected")
		return {frame.protected_ship,
		        {},
		        {},
		        frame.hit_context && frame.hit_context->target == frame.protected_ship && frame.hit_context->ordnance};
	if (role == "attacker")
		return {frame.attacker, frame.original_equipment, {}};
	// Legacy effect groups have two explicit entity arguments. New rule adapters
	// supply the richer frozen roles instead of deriving them from provenance text.
	if (role == "owner" || role == "subject" || role == "target")
		return effect_group_actor(frame, "protected");
	if (role == "source" || role == "instigator")
		return {frame.attacker, frame.original_equipment, {}};
	throw std::runtime_error("Unknown effect target role");
}

std::uint64_t EffectTransactionCommitter::epoch() const
{
	return epoch_;
}
void EffectTransactionCommitter::invalidate()
{
	if (epoch_ == std::numeric_limits<std::uint64_t>::max())
		throw std::runtime_error("Effect commit epoch exhausted");
	++epoch_;
}
std::string EffectTransactionCommitter::commit(PreparedEffectGroup &quote, RuleEventQueue &events,
                                               const EffectCommitCallbacks &callbacks)
{
	if (!quote.failure.empty())
		return quote.failure;
	if (quote.consumed)
		return "Effect quote was already consumed";
	if (quote.epoch != epoch_ || callbacks.stale(quote))
		return "Effect quote became stale";
	if (epoch_ == std::numeric_limits<std::uint64_t>::max())
		return "Effect commit epoch exhausted";
	if (quote.effect_failures.size() > 9007199254740991ULL - events.next_identity())
		return "Effect result identity exhausted";
	if (const auto failure = callbacks.origin_failure(quote); !failure.empty())
		return failure;
	if (quote.defer_damage && !quote.damages.empty())
		if (const auto failure = callbacks.damage_failure(quote); !failure.empty())
			return failure;
	if (!quote.stocks.commit())
		return "Store transaction became stale";
	quote.consumed = true;
	++epoch_;
	for (const auto &failure : quote.effect_failures)
		events.record(events.claim_identity(), "failed: " + failure);
	callbacks.apply(quote);
	return {};
}
void prepare_effect_application(const EffectApplication &application, ShieldComponent *shields,
                                PropertyEffects *properties, std::int64_t tick, const EffectApplicationCatalog &catalog,
                                const EffectApplicationTarget &target)
{
	validate_effect_identity(application.source, application.equipment, false);
	if (!target.available)
		throw std::runtime_error("Effect target unavailable");
	if (application.kind == EffectApplicationKind::Modifier)
	{
		if (!properties || !target.properties)
			throw std::runtime_error("Incompatible modifier owner");
		if (application.operation == EffectApplicationOperation::Remove)
		{
			remove_modifier_grant(*properties, application.source, application.equipment);
			return;
		}
		auto definition = application.definition;
		if (application.operation == EffectApplicationOperation::Refresh)
		{
			const auto found =
			    std::find_if(properties->applications.begin(), properties->applications.end(),
			                 [&](const auto &c)
			                 {
				                 return c.source == application.source && c.equipment == application.equipment;
			                 });
			if (found == properties->applications.end())
				throw std::runtime_error("Modifier grant unavailable for refresh");
			definition = found->definition;
		}
		const auto &compiled = catalog.modifiers.at(definition);
		if (!target.modifier_compatible(application.equipment, compiled))
			throw std::runtime_error("Incompatible modifier target");
		install_modifier_grant(*properties, compiled, catalog.properties, application.source, application.equipment,
		                       tick);
		return;
	}
	if (!shields || !target.shields || !application.equipment.empty())
		throw std::runtime_error("Incompatible shield grant owner");
	const auto matches = [&](const auto &c)
	{
		return c.equipment.empty() && c.source == application.source;
	};
	const auto found = std::find_if(shields->contributions.begin(), shields->contributions.end(), matches);
	if (application.operation == EffectApplicationOperation::Remove)
	{
		std::erase_if(shields->contributions, matches);
		return;
	}
	if (application.operation == EffectApplicationOperation::Refresh)
	{
		if (found == shields->contributions.end())
			throw std::runtime_error("Shield grant unavailable for refresh");
		const auto expiry = effect_expiry(tick, catalog.shields.at(found->definition_id).duration_ticks);
		for (auto &c : shields->contributions)
			if (matches(c))
				c.expiry = refresh_effect_expiry(c.expiry, expiry);
		return;
	}
	// A shield reserve is an intentional instance: a repeated apply must never
	// recreate or refill it. Modifier applies deliberately refresh their handle.
	if (found != shields->contributions.end())
		throw std::runtime_error("Shield grant name already active; refresh it explicitly");
	const auto &compiled = catalog.shields.at(application.definition);
	install_shield_grant(*shields, compiled, application.source, {}, application.definition,
	                     effect_expiry(tick, compiled.duration_ticks));
}
void prepare_group_modifier(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                            const EffectOperation &operation, const std::string &equipment,
                            const ModifierEffectDefinition &definition, const PropertyCatalog &properties,
                            const ModifierPreparationPort &port)
{
	auto compiled = definition;
	const auto role = operation.role == "$role" ? frame.rule_context ? frame.rule_context->property_role : std::string{}
	                                            : operation.role;
	if (operation.role == "$role" && role.empty())
		throw std::runtime_error("Required modifier role unavailable");
	if (!role.empty())
		std::erase_if(compiled.contributions,
		              [&](const auto &contribution)
		              {
			              return contribution.role != role;
		              });
	if (frame.rule_context && !frame.rule_context->property_filter.empty())
		std::erase_if(compiled.contributions,
		              [&](const auto &contribution)
		              {
			              return contribution.property != frame.rule_context->property_filter;
		              });
	if (compiled.contributions.empty() || !port.compatible(compiled))
		throw std::runtime_error("Incompatible modifier role/target");
	for (auto &contribution : compiled.contributions)
	{
		const auto delta = operation.delta_permille.value_or(contribution.delta_permille);
		if (delta < -100000 || delta > 100000)
			throw std::runtime_error("Modifier delta outside supported bounds");
		contribution.delta_permille = delta * frame.output_permille / 1000;
	}
	const auto source = frame.source + "." + operation.grant;
	validate_effect_identity(source, equipment, false);
	auto &prepared = port.prepared();
	const auto existing = std::find_if(prepared.applications.begin(), prepared.applications.end(),
	                                   [&](const auto &application)
	                                   {
		                                   return application.source == source && application.equipment == equipment;
	                                   });
	if (existing != prepared.applications.end() && existing->definition != compiled.id)
		throw std::runtime_error("Modifier grant name belongs to another definition");
	if (existing == prepared.applications.end() || !frame.maintained)
		install_modifier_grant(prepared, compiled, properties, source, equipment, quote.tick, frame.maintained);
	for (auto &application : prepared.applications)
		if (application.source == source && application.equipment == equipment)
			for (auto &contribution : application.modifier.values)
				for (const auto &value : compiled.contributions)
					if (contribution.name == value.name &&
					    (contribution.custom.empty() ? contribution.property == property_key(value.property)
					                                 : contribution.custom == value.property))
						contribution.delta = value.delta_permille;
}
void prepare_native_outcome(const EffectGroupInvocation &frame, const EffectOperation &operation,
                            const NativeOutcomePreparationPort &port, std::int64_t rate)
{
	const auto actor = effect_group_actor(frame, operation.target);
	if (actor.ordnance || actor.kind == PayloadOutcomeKind::Shield)
		throw std::runtime_error("Native repair/clinical outcome requires an ordinary native owner");
	const auto native = port.target(actor.entity);
	if (native.terminal)
		throw std::runtime_error("Native repair/clinical target is terminal");
	if (operation.kind == EffectOperationKind::StructuralRepair || operation.kind == EffectOperationKind::FaultRepair)
	{
		const auto *structure = native.structure;
		if (!structure)
			throw std::runtime_error("Native repair target has no structure");
		auto &prepared = port.structure(actor.entity);
		if (operation.kind == EffectOperationKind::StructuralRepair)
		{
			std::optional<std::size_t> compartment;
			if (!operation.compartment.empty())
			{
				const auto found = structure->definition.compartment_ids.find(operation.compartment);
				if (found == structure->definition.compartment_ids.end())
					throw std::runtime_error("Unknown structural repair compartment");
				compartment = found->second;
			}
			else if (actor.kind == PayloadOutcomeKind::Compartment || actor.kind == PayloadOutcomeKind::System)
				compartment = actor.compartment;
			if (!compartment || *compartment >= prepared.compartments.size() || operation.amount < 0 ||
			    operation.amount > resource_limit)
				throw std::runtime_error("Structural repair requires a bounded amount and explicit compartment");
			native_structural_repair(prepared.compartments.at(*compartment),
			                         structure->definition.compartments.at(*compartment),
			                         operation.amount * rate * 1000, rate * 1000);
		}
		else
		{
			const auto equipment = operation.equipment.empty() ? actor.equipment : operation.equipment;
			const auto found = structure->definition.faults.find(equipment);
			if (found == structure->definition.faults.end())
				throw std::runtime_error("Fault repair requires an explicit fitted system");
			const auto fault = std::find_if(found->second.begin(), found->second.end(),
			                                [&](const auto &definition)
			                                {
				                                return definition.id == operation.fault;
			                                });
			if (fault == found->second.end())
				throw std::runtime_error("Unknown repair fault identity");
			const auto index = static_cast<std::size_t>(fault - found->second.begin());
			if (!native_finish_fault_repair(prepared.faults.at(equipment).at(index), *fault))
				throw std::runtime_error("Fault is inactive or permanently destroys its system");
		}
		return;
	}
	const auto officer = !operation.officer.empty()    ? operation.officer
	                     : operation.equipment.empty() ? actor.officer
	                                                   : std::string{};
	const auto equipment = operation.equipment.empty() ? actor.equipment : operation.equipment;
	if (!officer.empty())
	{
		if (!operation.equipment.empty())
			throw std::runtime_error("Clinical outcome selects one patient");
		const auto patient = port.officer(actor.entity, officer);
		if (!patient)
			throw std::runtime_error("Unknown clinical officer");
		auto &prepared = port.patient(actor.entity, *patient);
		const auto changed = operation.kind == EffectOperationKind::Treat       ? native_treat_patient(prepared)
		                     : operation.kind == EffectOperationKind::Stabilise ? native_stabilise_patient(prepared)
		                                                                        : native_recover_patient(prepared);
		if (!changed)
			throw std::runtime_error("Invalid clinical transition");
		return;
	}
	if (operation.kind != EffectOperationKind::Treat || equipment.empty())
		throw std::runtime_error("Clinical outcome requires an explicit officer or treatment team");
	if (!port.team(actor.entity, equipment))
		throw std::runtime_error("Treatment equipment is not a native team");
	auto &prepared = port.clinical_team(actor.entity, equipment);
	if (!native_treat_team(prepared))
		throw std::runtime_error("Treatment team is not injured");
}
} // namespace phoenix
