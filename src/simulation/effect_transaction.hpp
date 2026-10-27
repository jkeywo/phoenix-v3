#pragma once
#include "native_outcomes.hpp"
namespace phoenix
{
// A quote owns its native copies and the addresses staged by StockTransaction.
// Moving a std::map preserves those addresses; copying this aggregate would not.
struct PreparedEffectPayment
{
	ResourceNetwork network;
	std::uint64_t revision = 0;
	std::vector<std::uint64_t> stores, coolant;
	// A native allocation may own the final network. Preparation still mutates
	// only this copy; the existing allocation commits after this transaction.
	ResourceNetwork *destination = nullptr;
	explicit PreparedEffectPayment(const ResourceNetwork &original, ResourceNetwork *prepared_destination = nullptr)
	    : network(original), revision(original.revision()), destination(prepared_destination)
	{
		for (const auto &store : original.stores())
			stores.push_back(store.stock.revision());
		for (const auto &source : original.coolant_sources())
			coolant.push_back(source.stock.revision());
	}
};
struct EffectGroupInvocation
{
	EffectGroupDefinition execution;
	EntityId protected_ship = 0, attacker = 0;
	std::string source, original_equipment;
	std::optional<PayloadHitContext> hit_context;
	std::optional<NativeRuleContext> rule_context;
	std::int64_t output_permille = 1000;
	bool maintained = false;
};
struct PreparedEffectDamage
{
	EntityId target = 0;
	std::int64_t amount = 0;
	bool outcome = false;
	PayloadHitContext context;
	std::size_t invocation = 0;
};
struct PreparedEffectTarget
{
	EntityId target = 0;
	bool outcome = false, ordnance = false;
};
struct PreparedEffectGroup
{
	std::vector<EffectGroupInvocation> invocations;
	std::vector<std::size_t> invocation_roots;
	std::size_t failed_invocation = static_cast<std::size_t>(-1);
	std::string failure;
	std::int64_t tick = 0;
	std::uint64_t epoch = 0, next_active = 0, original_next_active = 0;
	bool consumed = false, defer_damage = false;
	bool pre_generation = false;
	std::map<EntityId, NativeStructureOutcome> structures, original_structures;
	std::map<std::pair<EntityId, std::size_t>, NativePatientOutcome> patients, original_patients;
	std::map<std::pair<EntityId, std::string>, NativeTeamOutcome> clinical_teams, original_clinical_teams;
	std::map<EntityId, ShieldComponent> shields, original_shields;
	std::map<EntityId, PropertyEffects> properties, original_properties;
	std::map<CustomPropertyAddress, CustomPropertyState> custom_properties, original_custom_properties;
	std::map<EntityId, AbilityRuntime> abilities, original_abilities;
	std::map<CustomPropertyAddress, ProcessRuntime> processes, original_processes;
	std::set<std::pair<CustomPropertyAddress, std::string>> process_stops;
	std::set<std::pair<CustomPropertyAddress, std::string>> prepaid_process_stops;
	std::set<std::pair<CustomPropertyAddress, std::string>> prepaid_process_exits;
	std::vector<std::string> effect_failures;
	std::map<EntityId, PreparedEffectPayment> payments;
	std::set<EntityId> changed_inventories;
	std::vector<PreparedEffectTarget> targets;
	std::vector<std::pair<EntityId, std::string>> equipment;
	std::vector<PreparedEffectDamage> damages;
	std::optional<std::vector<ActiveRuleEffect>> active_effects, original_active_effects;
	StockTransaction stocks;
	PreparedEffectGroup() = default;
	PreparedEffectGroup(const PreparedEffectGroup &) = delete;
	PreparedEffectGroup &operator=(const PreparedEffectGroup &) = delete;
	PreparedEffectGroup(PreparedEffectGroup &&) = default;
	PreparedEffectGroup &operator=(PreparedEffectGroup &&) = default;
};

inline bool same_effect_properties(const PropertyEffects &a, const PropertyEffects &b)
{
	return a == b;
}
inline bool same_effect_shields(const ShieldComponent &a, const ShieldComponent &b)
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
inline bool same_effect_abilities(const AbilityRuntime &a, const AbilityRuntime &b)
{
	return a == b;
}
inline bool same_effect_active_rules(const std::vector<ActiveRuleEffect> &a, const std::vector<ActiveRuleEffect> &b)
{
	return a == b;
}
} // namespace phoenix
