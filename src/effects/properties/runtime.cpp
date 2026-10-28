module;
#include <flecs.h>
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

module phoenix.effects.properties.runtime;
import phoenix.gameplay.fitting;
import phoenix.gameplay.combat.state;
import phoenix.data;

namespace phoenix
{
PropertyTargetKey modifier_target_key(flecs::entity ship, const std::string &name)
{
	if (const auto *equipment = ship.try_get<EquipmentIndex>())
		if (const auto at = equipment->by_id.find(name); at != equipment->by_id.end())
			return {PropertyTargetKind::Equipment, at->second.id(), {}};
	if (name.starts_with("effect:"))
	{
		const auto separator = name.find(':', 7);
		std::uint64_t id = 0;
		if (separator != std::string::npos)
		{
			const auto parsed = std::from_chars(name.data() + 7, name.data() + separator, id);
			if (parsed.ec == std::errc{} && parsed.ptr == name.data() + separator)
				return {PropertyTargetKind::Shield, id, name.substr(separator + 1)};
		}
	}
	return {PropertyTargetKind::Logical, 0, name};
}

PropertyTargetKey modifier_target_key(const EffectTarget &target)
{
	if (target.kind == EffectTargetKind::Equipment)
		return {PropertyTargetKind::Equipment, target.equipment.id(), {}};
	if (target.kind == EffectTargetKind::Shield)
		return {PropertyTargetKind::Shield, target.contribution, target.contribution_name};
	return {PropertyTargetKind::Logical, 0, target.equipment_name};
}

const PropertyEffectOwner *rebuild_property_aggregates(flecs::entity ship)
{
	const auto *owner = ship.try_get<PropertyEffectOwner>();
	if (!owner || owner->aggregate_valid)
		return owner;
	owner->independent.clear();
	owner->guarded.clear();
	for (const auto &[id, effect] : owner->applications)
	{
		const auto &identity = effect.get<ActiveEffectIdentity>();
		const auto key = modifier_target_key(effect.get<EffectTarget>());
		auto &totals =
		    identity.source.starts_with("rule.") ? owner->guarded[key][identity.source] : owner->independent[key];
		for (const auto &row : effect.get<ModifierValues>().values)
			if (row.custom.empty())
				totals.native[static_cast<std::size_t>(row.property)] += row.delta;
			else
				merge_property_aggregate(totals.custom[row.custom], {row.delta, row.override_value, row.priority, id});
	}
	owner->aggregate_valid = true;
	return owner;
}

PropertyAggregateValue read_modifier_aggregate(flecs::entity ship, const PropertyTargetKey &key, PropertyKey property,
                                               const std::string &custom, std::map<std::string, bool> &grants,
                                               const std::function<bool(const std::string &)> &grant_available)
{
	PropertyAggregateValue result;
	const auto *owner = rebuild_property_aggregates(ship);
	if (!owner)
		return result;
	const auto select = [&](const PropertyTotals &totals)
	{
		if (custom.empty())
			return PropertyAggregateValue{totals.native[static_cast<std::size_t>(property)]};
		const auto at = totals.custom.find(custom);
		return at == totals.custom.end() ? PropertyAggregateValue{} : at->second;
	};
	if (const auto at = owner->independent.find(key); at != owner->independent.end())
		result = select(at->second);
	if (const auto at = owner->guarded.find(key); at != owner->guarded.end())
		for (const auto &[source, totals] : at->second)
		{
			const auto value = select(totals);
			if (!value.delta && !value.override_value)
				continue;
			auto known = grants.find(source);
			if (known == grants.end())
			{
				const bool available = grant_available(source);
				known = grants.emplace(source, available).first;
			}
			if (known->second)
				merge_property_aggregate(result, value);
		}
	return result;
}

EffectTarget resolve_property_target(flecs::entity ship, const std::string &equipment)
{
	EffectTarget target;
	target.body = ship;
	target.equipment_name = equipment;
	const auto &index = ship.get<EquipmentIndex>().by_id;
	const auto fitted = index.find(equipment);
	if (fitted != index.end())
	{
		target.kind = EffectTargetKind::Equipment;
		target.equipment = fitted->second;
	}
	else if (const auto key = modifier_target_key(ship, equipment); key.kind == PropertyTargetKind::Shield)
	{
		target.kind = EffectTargetKind::Shield;
		target.contribution = key.identity;
		target.contribution_name = key.name;
	}
	return target;
}

void PropertyApplicationStore::initialise(flecs::world &ecs)
{
	modifiers_ =
	    ecs.query_builder<const ActiveEffectIdentity, const EffectTarget, const EffectLifetime, const ModifierValues>()
	        .cached()
	        .build();
	owners_ = ecs.query_builder<const StableId, const PropertyEffectOwner>()
	              .with(flecs::Disabled)
	              .optional()
	              .cached()
	              .build();
}

void PropertyApplicationStore::erase(flecs::entity effect, const PropertyMutationObserver &observer)
{
	const auto effect_target = effect.get<EffectTarget>();
	observer.before_removal(effect_target);
	const auto target = effect.get<EffectTarget>();
	const auto id = effect.get<ActiveEffectIdentity>().id;
	if (target.body.is_alive())
		if (auto *owner = target.body.try_get_mut<PropertyEffectOwner>())
		{
			const auto &identity = effect.get<ActiveEffectIdentity>();
			owner->grants.erase({identity.source, target.equipment_name});
			owner->applications.erase(id);
			owner->aggregate_valid = false;
		}
	effect.destruct();
	observer.after_change();
}

void PropertyApplicationStore::commit(flecs::world &ecs, flecs::entity ship, const PropertyEffects &snapshot,
                                      const PropertyMutationObserver &observer)
{
	observer.before_owner_change(ship);
	std::set<std::uint64_t> retained;
	for (const auto &application : snapshot.applications)
		retained.insert(application.id);
	std::vector<flecs::entity> removed;
	for (const auto &[id, effect] : ship.get<PropertyEffectOwner>().applications)
		if (!retained.contains(id))
			removed.push_back(effect);
	for (auto effect : removed)
		erase(effect, observer);
	for (const auto &application : snapshot.applications)
	{
		const auto &applications = ship.get<PropertyEffectOwner>().applications;
		if (const auto at = applications.find(application.id); at != applications.end())
		{
			auto effect = at->second;
			if (effect.get<EffectLifetime>().expiry != application.expiry)
				effect.set<EffectLifetime>({application.expiry});
			if (!(effect.get<ModifierValues>() == application.modifier))
			{
				effect.set<ModifierValues>(application.modifier);
				ship.get_mut<PropertyEffectOwner>().aggregate_valid = false;
			}
		}
		else
		{
			auto effect = ecs.entity()
			                  .set<ActiveEffectIdentity>(
			                      {application.id, ship.get<StableId>().id, application.source, application.definition})
			                  .set<EffectTarget>(resolve_property_target(ship, application.equipment))
			                  .set<EffectLifetime>({application.expiry})
			                  .set<ModifierValues>(application.modifier);
			auto &owner = ship.get_mut<PropertyEffectOwner>();
			owner.applications.emplace(application.id, effect);
			owner.grants.emplace(std::pair{application.source, application.equipment}, effect);
			owner.aggregate_valid = false;
		}
	}
	ship.get_mut<PropertyEffectOwner>().next_id = snapshot.next_id;
	observer.after_change();
}

void PropertyApplicationStore::expire(std::int64_t tick, const PropertyMutationObserver &observer)
{
	std::vector<flecs::entity> expired;
	modifiers_.each(
	    [&](flecs::entity e, const ActiveEffectIdentity &, const EffectTarget &, const EffectLifetime &life,
	        const ModifierValues &)
	    {
		    if (life.expiry >= 0 && life.expiry <= tick)
			    expired.push_back(e);
	    });
	for (auto effect : expired)
		erase(effect, observer);
}

void PropertyApplicationStore::prune(const ModifierLogicalTargetAvailable &logical_available,
                                     const PropertyMutationObserver &observer)
{
	std::vector<flecs::entity> invalid;
	modifiers_.each(
	    [&](flecs::entity effect, const ActiveEffectIdentity &, const EffectTarget &target, const EffectLifetime &,
	        const ModifierValues &)
	    {
		    bool available = target.body.is_alive();
		    if (available && target.kind == EffectTargetKind::Equipment)
			    available = target.equipment.is_alive() && target.equipment.has<Equipment>() &&
			                target.equipment.parent() == target.body;
		    if (available && target.kind == EffectTargetKind::Shield)
		    {
			    const auto *shields = target.body.try_get<ShieldComponent>();
			    available = shields && std::any_of(shields->contributions.begin(), shields->contributions.end(),
			                                       [&](const auto &c)
			                                       {
				                                       return c.effect_id == target.contribution &&
				                                              c.definition.name == target.contribution_name;
			                                       });
		    }
		    if (available && target.kind == EffectTargetKind::Logical)
			    available = logical_available(target.body, target.equipment_name);
		    if (!available)
			    invalid.push_back(effect);
	    });
	for (auto effect : invalid)
		erase(effect, observer);
}

Json PropertyApplicationStore::save() const
{
	Json owners = Json::array(), applications = Json::array();
	std::map<EntityId, flecs::entity> ordered;
	owners_.each(
	    [&](flecs::entity ship, const StableId &id, const PropertyEffectOwner &)
	    {
		    ordered.emplace(id.id, ship);
	    });
	for (const auto &[id, ship] : ordered)
	{
		const auto &owner = ship.get<PropertyEffectOwner>();
		owners.push_back({{"owner", id}, {"next_id", owner.next_id}});
		for (const auto &[key, effect] : owner.applications)
		{
			const auto &identity = effect.get<ActiveEffectIdentity>();
			const auto &values = effect.get<ModifierValues>();
			Json named = Json::array();
			for (const auto &v : values.values)
				named.push_back({{"name", v.name}, {"delta_permille", v.delta}});
			applications.push_back({{"owner", id},
			                        {"id", key},
			                        {"source", identity.source},
			                        {"definition", identity.definition},
			                        {"equipment", effect.get<EffectTarget>().equipment_name},
			                        {"expiry", effect.get<EffectLifetime>().expiry},
			                        {"maintained", values.maintained},
			                        {"values", named}});
		}
	}
	return {{"modifier_owners", owners}, {"modifiers", applications}};
}

void PropertyApplicationStore::restore(flecs::world &ecs, const Json &saved, std::size_t owner_bound,
                                       const ModifierOwnerLookup &find_owner,
                                       const std::map<std::string, ModifierEffectDefinition> &definitions,
                                       const PropertyCatalog &catalogue, const ModifierTargetCompatible &compatible,
                                       const PropertyMutationObserver &observer)
{
	if (saved.size() != 4 || !saved.at("modifier_owners").is_array() || !saved.at("modifiers").is_array() ||
	    saved.at("modifier_owners").size() > owner_bound)
		throw std::runtime_error("Invalid active modifier roster");
	std::map<EntityId, PropertyEffects> staged;
	for (const auto &owner : saved.at("modifier_owners"))
	{
		const auto id = checked_integer(owner.at("owner"), 1, 9007199254740991LL, "modifier.owner");
		if (owner.size() != 2 || !bool(find_owner(id)) || !find_owner(id).has<PropertyEffectOwner>() ||
		    !staged
		         .emplace(id, PropertyEffects{static_cast<std::uint64_t>(checked_integer(
		                                          owner.at("next_id"), 1, 9007199254740991LL, "modifier.next_id")),
		                                      {}})
		         .second)
			throw std::runtime_error("Invalid modifier owner");
	}
	std::size_t expected = 0;
	owners_.each(
	    [&](const StableId &, const PropertyEffectOwner &)
	    {
		    ++expected;
	    });
	if (staged.size() != expected || saved.at("modifiers").size() > expected * modifier_contribution_limit)
		throw std::runtime_error("Incomplete modifier owner roster");
	std::set<std::pair<EntityId, std::uint64_t>> identities;
	for (const auto &row : saved.at("modifiers"))
	{
		const auto owner = checked_integer(row.at("owner"), 1, 9007199254740991LL, "modifier.owner");
		const auto id = checked_integer(row.at("id"), 1, 9007199254740991LL, "modifier.id");
		if (row.size() != 8 || !staged.contains(owner) || !identities.emplace(owner, id).second ||
		    !row.at("values").is_array() || row.at("values").empty() || row.at("values").size() > 64)
			throw std::runtime_error("Invalid modifier application");
		auto &properties = staged.at(owner);
		ModifierApplication application;
		application.id = checked_integer(row.at("id"), 1, properties.next_id - 1, "modifier.id");
		application.source = row.at("source").get<std::string>();
		application.definition = row.at("definition").get<std::string>();
		application.equipment = row.at("equipment").get<std::string>();
		validate_effect_identity(application.source, application.equipment, false);
		application.expiry = checked_integer(row.at("expiry"), -1, 9007199254740991LL, "modifier.expiry");
		if (!row.at("maintained").is_boolean())
			throw std::runtime_error("Invalid modifier lifetime owner");
		application.modifier.maintained = row.at("maintained").get<bool>();
		const auto &definition = definitions.at(application.definition);
		if ((definition.duration_ticks == 0) != (application.expiry == -1))
			throw std::runtime_error("Invalid saved modifier expiry");
		std::set<std::string> names;
		for (const auto &value : row.at("values"))
		{
			if (value.size() != 2)
				throw std::runtime_error("Invalid modifier named value");
			const auto name = value.at("name").get<std::string>();
			const auto entry = std::find_if(definition.contributions.begin(), definition.contributions.end(),
			                                [&](const auto &d)
			                                {
				                                return d.name == name;
			                                });
			if (entry == definition.contributions.end() || !names.insert(name).second)
				throw std::runtime_error("Invalid saved modifier contribution");
			const auto delta = checked_integer(value.at("delta_permille"), -100000, 100000, "modifier.delta_permille");
			if (catalogue.contains(entry->property))
				application.modifier.values.push_back(
				    {name, PropertyKey::count, delta, entry->property, entry->value, entry->priority});
			else
				application.modifier.values.push_back({name, property_key(entry->property), delta});
		}
		if (!compatible(find_owner(owner), application.equipment, application.modifier.values))
			throw std::runtime_error("Incompatible saved modifier target/subset");
		properties.applications.push_back(std::move(application));
	}
	for (const auto &[owner, properties] : staged)
	{
		std::size_t independent = 0, maintained = 0;
		std::set<std::pair<std::string, std::string>> grants;
		for (const auto &application : properties.applications)
		{
			if (!grants.emplace(application.source, application.equipment).second)
				throw std::runtime_error("Duplicate modifier application");
			(application.modifier.maintained ? maintained : independent) += application.modifier.values.size();
		}
		if (independent > independent_modifier_contribution_limit ||
		    maintained > maintained_modifier_contribution_limit)
			throw std::runtime_error("Modifier application capacity exceeded");
		commit(ecs, find_owner(owner), properties, observer);
	}
}

} // namespace phoenix
