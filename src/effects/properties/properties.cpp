module;
#include <flecs.h>
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

module phoenix.effects.properties;

namespace phoenix
{
// Divide before multiplying so high-resolution work quantities retain checked arithmetic.
std::int64_t apply_property_delta(std::int64_t base, std::int64_t delta, std::int64_t maximum)
{
	if (base < 0 || maximum < 0 || delta < -modifier_delta_limit || delta > modifier_delta_limit)
		throw std::runtime_error("Property arithmetic outside supported bounds");
	const auto scale = std::max<std::int64_t>(0, 1000 + delta);
	if (!scale || !base)
		return 0;
	const auto fraction = (base % 1000) * scale / 1000;
	// Saturate before multiplication, including for high-resolution repair work.
	if (fraction >= maximum || base / 1000 > (maximum - fraction) / scale)
		return maximum;
	return base / 1000 * scale + fraction;
}

void merge_property_aggregate(PropertyAggregateValue &total, const PropertyAggregateValue &value)
{
	total.delta += value.delta;
	if (value.override_value && (value.priority > total.priority ||
	                             (value.priority == total.priority && value.application > total.application)))
	{
		total.override_value = value.override_value;
		total.priority = value.priority;
		total.application = value.application;
	}
}

std::vector<PropertyValue> modifier_values(const ModifierEffectDefinition &definition, const PropertyCatalog &catalog)
{
	std::vector<PropertyValue> values;
	values.reserve(definition.contributions.size());
	for (const auto &value : definition.contributions)
		if (catalog.contains(value.property))
			values.push_back(
			    {value.name, PropertyKey::count, value.delta_permille, value.property, value.value, value.priority});
		else
			values.push_back({value.name, property_key(value.property), value.delta_permille});
	return values;
}

std::uint64_t install_modifier_grant(PropertyEffects &owner, const ModifierEffectDefinition &definition,
                                     const PropertyCatalog &catalog, const std::string &source,
                                     const std::string &equipment, std::int64_t tick, bool maintained)
{
	validate_effect_identity(source, equipment, false);
	const auto expiry = effect_expiry(tick, definition.duration_ticks);
	std::size_t count = 0;
	for (auto &application : owner.applications)
	{
		if (application.source == source && application.equipment == equipment)
		{
			if (application.definition != definition.id)
				throw std::runtime_error("Modifier grant name belongs to another definition");
			if (application.modifier.maintained != maintained)
				throw std::runtime_error("Modifier grant changed lifetime owner");
			application.expiry = refresh_effect_expiry(application.expiry, expiry);
			return application.id;
		}
		if (application.modifier.maintained == maintained)
			count += application.modifier.values.size();
	}
	const auto limit = maintained ? maintained_modifier_contribution_limit : independent_modifier_contribution_limit;
	if (count + definition.contributions.size() > limit || owner.next_id >= 9007199254740991ULL)
		throw std::runtime_error("Modifier application capacity exhausted");
	const auto id = owner.next_id++;
	owner.applications.push_back(
	    {id, source, definition.id, equipment, expiry, {maintained, modifier_values(definition, catalog)}});
	return id;
}

void remove_modifier_grant(PropertyEffects &owner, const std::string &source, const std::string &equipment)
{
	validate_effect_identity(source, equipment, false);
	std::erase_if(owner.applications,
	              [&](const auto &application)
	              {
		              return application.source == source && application.equipment == equipment;
	              });
}
ModifierApplication read_modifier_application(flecs::entity effect)
{
	const auto &identity = effect.get<ActiveEffectIdentity>();
	return {identity.id,
	        identity.source,
	        identity.definition,
	        effect.get<EffectTarget>().equipment_name,
	        effect.get<EffectLifetime>().expiry,
	        effect.get<ModifierValues>()};
}

PropertyEffects read_property_effects(flecs::entity ship)
{
	PropertyEffects snapshot;
	if (const auto *owner = ship.try_get<PropertyEffectOwner>())
	{
		snapshot.next_id = owner->next_id;
		snapshot.applications.reserve(owner->applications.size());
		for (const auto &[id, effect] : owner->applications)
			snapshot.applications.push_back(read_modifier_application(effect));
	}
	return snapshot;
}
} // namespace phoenix
