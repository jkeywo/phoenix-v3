module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::custom_property_state(const CustomPropertyAddress &address) const -> CustomPropertyState *
{
	const auto at = entities.find(address.entity);
	return at == entities.end() ? nullptr
	                            : phoenix::custom_property_state(at->second, address,
	                                                             native_owner_definitions(address.entity).personnel);
}

auto Runtime::validate_custom_property_value(const std::string &id, const CustomPropertyValue &value) const -> void
{
	(void)read_custom_property_value(property_catalogue->at(id), custom_property_json(value), "property." + id);
}

auto Runtime::effective_custom_property(const CustomPropertyAddress &address, const std::string &id,
                                        const CustomPropertyState &state, bool retiring) const -> CustomPropertyValue
{
	const auto &definition = property_catalogue->at(id);
	const auto &base = state.bases.at(id);
	if (definition.kind == CustomPropertyKind::Quantity)
		return base;
	const auto ship = entities.at(address.entity);
	const auto token = custom_modifier_target(RuleActor{
	    address.entity, address.equipment, address.officer, false,
	    address.compartment >= 0 ? PayloadOutcomeKind::Compartment : PayloadOutcomeKind::Entity, address.compartment});
	RuleSourceAvailabilityCache sources;
	std::map<std::string, bool> grants;
	const auto value = read_property_aggregate(ship, property_target_key(ship, token), PropertyKey::count, id, sources,
	                                           grants, retiring);
	if (definition.kind != CustomPropertyKind::Attribute)
		return value.override_value.value_or(base);
	const auto number = std::get<std::int64_t>(base);
	const auto sign = number < 0 ? -1 : 1;
	const auto magnitude = number < 0 ? -number : number;
	const auto bound = std::max(std::abs(definition.minimum), std::abs(definition.maximum));
	return std::clamp(sign * apply_property_delta(magnitude, value.delta, bound), definition.minimum,
	                  definition.maximum);
}

auto Runtime::read_custom_property(const RuleActor &actor, const std::string &id, bool base) const
    -> std::optional<state_tree::Value>
{
	try
	{
		const auto address = custom_property_address(actor);
		const auto *state = custom_property_state(address);
		if (!state || !state->bases.contains(id))
			return {};
		return custom_property_scalar(base ? state->bases.at(id) : effective_custom_property(address, id, *state));
	}
	catch (const std::exception &)
	{
		return {};
	}
}

auto Runtime::custom_property_addresses(EntityId id) const -> std::vector<CustomPropertyAddress>
{
	return phoenix::custom_property_addresses(entities.at(id), native_owner_definitions(id).personnel);
}

auto Runtime::mark_custom_property_rates_dirty() -> void
{
	// Native owners are already closing. Reconcile their derived suffixes once
	// after the surrounding retirement/transaction boundary has published.
	custom_property_native_rates_dirty = true;
}

auto Runtime::erase_custom_property_rate_source(const std::string &source) -> void
{
	retired_custom_property_rates.insert(source + ":");
}

auto Runtime::prune_custom_property_rates(std::span<const NativeRuleSource> sources) -> void
{
	if (retired_custom_property_rates.empty() && !custom_property_native_rates_dirty)
		return;
	std::set<std::string> live;
	if (custom_property_native_rates_dirty)
	{
		for (const auto &source : sources)
			if (source.context.owner_effect_kind != NativeEffectKind::None &&
			    native_property_rate_owner_live(source.context))
				live.insert(native_property_rate_suffix(source.context));
		// Process work and lifecycle groups have native provenance even when
		// neither the process nor its grant declares rules. Derive those live
		// lifetimes from their actual owners, including temporarily paused runs.
		for (const auto &[id, ship] : entities)
			for (const auto &address : custom_property_addresses(id))
				if (const auto *runtime = process_runtime(address))
					for (const auto &[name, process] : runtime->processes)
					{
						auto context = process_context(address, process);
						if (native_property_rate_owner_live(context))
							live.insert(native_property_rate_suffix(context));
						if (!process.source.empty())
						{
							context.owner_grant = process.source;
							if (native_property_rate_owner_live(context))
								live.insert(native_property_rate_suffix(context));
						}
					}
	}
	for (const auto &[id, ship] : entities)
		for (const auto &address : custom_property_addresses(id))
			if (auto *state = custom_property_state(address))
				std::erase_if(state->remainders,
				              [&](const auto &entry)
				              {
					              const auto separator = entry.first.find(':');
					              if (separator != std::string::npos &&
					                  retired_custom_property_rates.contains(entry.first.substr(0, separator + 1)))
						              return true;
					              const auto suffix = entry.first.rfind(":owner.");
					              return custom_property_native_rates_dirty && suffix != std::string::npos &&
					                     !live.contains(entry.first.substr(suffix));
				              });
	retired_custom_property_rates.clear();
	custom_property_native_rates_dirty = false;
}

auto Runtime::commit_custom_property_state(const CustomPropertyAddress &address, const CustomPropertyState &value)
    -> void
{
	auto *state = custom_property_state(address);
	if (!state || state->bases.size() != value.bases.size())
		throw std::runtime_error("Custom property owner changed");
	for (const auto &[id, base] : value.bases)
	{
		if (!state->bases.contains(id))
			throw std::runtime_error("Custom property is undeclared");
		validate_custom_property_value(id, base);
	}
	if (value.remainders.size() > custom_property_rate_limit)
		throw std::runtime_error("Property rate remainder capacity exhausted");
	for (const auto &[id, remainder] : value.remainders)
		if (id.empty() || id.size() > 512 || remainder <= -rate || remainder >= rate)
			throw std::runtime_error("Invalid property rate remainder");
	begin_custom_property_batch();
	capture_custom_property_owner(address);
	*state = value;
	finish_custom_property_batch();
}

auto Runtime::custom_property_declarations(const CustomPropertyAddress &address) const -> const CustomPropertyState &
{
	static const CustomPropertyState empty;
	const auto &compiled = definition(address.entity);
	if (!address.equipment.empty())
	{
		const auto at = compiled.equipment_properties.find(address.equipment);
		return at == compiled.equipment_properties.end() ? empty : at->second;
	}
	if (!address.officer.empty())
	{
		const auto at = compiled.officer_properties.find(address.officer);
		return at == compiled.officer_properties.end() ? empty : at->second;
	}
	if (address.compartment >= 0)
	{
		if (!compiled.combat || static_cast<std::size_t>(address.compartment) >= compiled.combat->compartments.size())
			return empty;
		const auto at = compiled.compartment_properties.find(compiled.combat->compartments[address.compartment].id);
		return at == compiled.compartment_properties.end() ? empty : at->second;
	}
	return compiled.properties;
}

auto Runtime::save_custom_properties() const -> Json
{
	Json out = Json::array();
	for (const auto &[id, ship] : entities)
		if (id < field_identity_base)
			for (const auto &address : custom_property_addresses(id))
			{
				const auto *state = custom_property_state(address);
				if (!state || state->bases.empty())
					continue;
				Json values = Json::object();
				for (const auto &[key, value] : state->bases)
					values[key] = custom_property_json(value);
				out.push_back({{"entity", id},
				               {"equipment", address.equipment},
				               {"officer", address.officer},
				               {"compartment", address.compartment},
				               {"values", values},
				               {"remainders", state->remainders}});
			}
	return out;
}

auto Runtime::restore_custom_properties(const Json &saved) -> void
{
	if (!saved.is_array())
		throw std::runtime_error("Invalid custom properties");
	std::set<CustomPropertyAddress> seen;
	std::map<CustomPropertyAddress, CustomPropertyState> staged;
	for (const auto &row : saved)
	{
		if (!row.is_object() || row.size() != 6 || !row.at("values").is_object())
			throw std::runtime_error("Invalid custom property owner row");
		CustomPropertyAddress address{
		    static_cast<EntityId>(checked_integer(row.at("entity"), 1, 9007199254740991LL, "property.entity")),
		    row.at("equipment").get<std::string>(), row.at("officer").get<std::string>(),
		    checked_integer(row.at("compartment"), -1, 255, "property.compartment")};
		if ((!address.equipment.empty() && (!address.officer.empty() || address.compartment >= 0)) ||
		    (!address.officer.empty() && address.compartment >= 0) || !seen.insert(address).second)
			throw std::runtime_error("Invalid custom property owner identity");
		const auto *owner = custom_property_state(address);
		const auto &initial = custom_property_declarations(address);
		if (!owner || initial.bases.size() != row.at("values").size() || initial.bases.empty())
			throw std::runtime_error("Invalid custom property binding roster");
		CustomPropertyState state;
		for (const auto &[key, value] : row.at("values").items())
		{
			if (!initial.bases.contains(key))
				throw std::runtime_error("Undeclared saved property");
			state.bases[key] = read_custom_property_value(property_catalogue->at(key), value, "property." + key);
		}
		if (!row.at("remainders").is_object() || row.at("remainders").size() > custom_property_rate_limit)
			throw std::runtime_error("Invalid custom rate remainder roster");
		for (const auto &[key, remainder] : row.at("remainders").items())
			state.remainders[key] = checked_integer(remainder, -rate + 1, rate - 1, "property.remainder");
		for (const auto &[key, value] : state.remainders)
			if (key.empty() || key.size() > 512 || value <= -rate || value >= rate)
				throw std::runtime_error("Invalid custom rate remainder");
		staged.emplace(address, std::move(state));
	}
	for (const auto &[id, ship] : entities)
		if (id < field_identity_base)
			for (const auto &address : custom_property_addresses(id))
				if (!custom_property_declarations(address).bases.empty() && !seen.contains(address))
					throw std::runtime_error("Missing saved property owner");
	for (auto &[address, state] : staged)
		*custom_property_state(address) = std::move(state);
}

auto Runtime::begin_custom_property_batch() -> void
{
	++custom_property_batch_depth;
}

auto Runtime::capture_custom_property_owner(const CustomPropertyAddress &address, bool retiring) -> void
{
	if (custom_property_batch.contains(address))
		return;
	const auto *state = custom_property_state(address);
	if (!state || state->bases.empty())
		return;
	CustomPropertyBefore before{*state, {}};
	for (const auto &[id, value] : state->bases)
		before.effective[id] = effective_custom_property(address, id, *state, retiring);
	custom_property_batch.emplace(address, std::move(before));
}

auto Runtime::emit_custom_property_change(const CustomPropertyAddress &address, const CustomPropertyBefore &before)
    -> void
{
	const auto *state = custom_property_state(address);
	if (!state)
		return;
	const auto ship = entities.at(address.entity);
	RuleActor subject{address.entity,
	                  address.equipment,
	                  address.officer,
	                  false,
	                  address.compartment >= 0 ? PayloadOutcomeKind::Compartment : PayloadOutcomeKind::Entity,
	                  address.compartment};
	for (const auto &[id, base] : state->bases)
	{
		const auto effective = effective_custom_property(address, id, *state);
		if (before.state.bases.at(id) == base && before.effective.at(id) == effective)
			continue;
		NativeRuleContext event;
		event.subject = event.target = subject;
		event.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
		for (const auto &source : rule_sources(ship))
			visit_rule_event_bindings(
			    source, RuleTrigger::PropertyChanged, event,
			    [&](const std::string &key, const RuleDefinition &rule, NativeRuleContext context)
			    {
				    std::erase_if(context.captured,
				                  [&](const auto &capture)
				                  {
					                  return capture.fact.kind == RuleFactKind::PropertyBefore ||
					                         capture.fact.kind == RuleFactKind::PropertyAfter ||
					                         std::find(rule.facts.begin(), rule.facts.end(), capture.fact) ==
					                             rule.facts.end();
				                  });
				    for (const auto &fact : rule.facts)
					    if (fact.kind == RuleFactKind::PropertyBefore || fact.kind == RuleFactKind::PropertyAfter)
					    {
						    state_tree::Value value;
						    if (fact.key == id)
							    value = custom_property_scalar(
							        fact.kind == RuleFactKind::PropertyBefore
							            ? (fact.aux == "base" ? before.state.bases.at(id) : before.effective.at(id))
							            : (fact.aux == "base" ? base : effective));
						    context.captured.push_back({fact, value});
					    }
				    (void)enqueue_rule(rule.id, key, context, RuleEventPhase::Periodic, event.tick + 1);
			    });
	}
}

auto Runtime::finish_custom_property_batch() -> void
{
	if (!custom_property_batch_depth || --custom_property_batch_depth)
		return;
	auto changes = std::move(custom_property_batch);
	custom_property_batch.clear();
	if (!suppress_custom_property_events)
		for (const auto &[address, before] : changes)
			emit_custom_property_change(address, before);
}
} // namespace phoenix::world_detail
