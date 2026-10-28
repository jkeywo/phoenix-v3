module;
#include <flecs.h>
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

module phoenix.effects.preparation;

namespace phoenix
{
void EffectPreparation::stage(PreparedEffectGroup &quote, std::size_t invocation) const
{
	const auto &frame = quote.invocations.at(invocation);
	const auto &execution = frame.execution;
	const auto protected_ship = frame.protected_ship;
	const auto attacker = frame.attacker;
	const auto &source = frame.source;
	const auto &original_equipment = frame.original_equipment;
	const auto owner = [&](EntityId id, bool uses_payload_outcome = true, bool ordnance = false)
	{
		return target(quote, frame, id, uses_payload_outcome, ordnance);
	};
	const auto stock = [&](EntityId id, const std::string &name, bool uses_payload_outcome = true) -> StockStore &
	{
		return this->stock(quote, frame, id, name, uses_payload_outcome);
	};
	std::size_t operation_index = 0;
	for (const auto &operation : execution.operations)
	{
		const auto current_operation = operation_index++;
		if (operation.scope == "owner")
		{
			ports_.remove_owner(quote, frame, operation);
			continue;
		}
		auto op = operation;
		op.equipment = equipment(frame, op.equipment);
		const auto actor = effect_group_actor(frame, op.target);
		const auto id = actor.entity;
		const bool uses_payload_outcome =
		    frame.rule_context ? actor.kind != PayloadOutcomeKind::Entity || actor.ordnance
		                       : frame.hit_context && id == frame.hit_context->target &&
		                             actor.ordnance == frame.hit_context->ordnance && op.target != "attacker" &&
		                             op.target != "source" && op.target != "instigator";
		PayloadHitContext address{id};
		address.kind = actor.kind;
		address.ordnance = actor.ordnance;
		if (actor.kind == PayloadOutcomeKind::Compartment || actor.kind == PayloadOutcomeKind::System)
			address.compartment = actor.compartment;
		if (actor.kind == PayloadOutcomeKind::System)
			address.equipment = actor.equipment;
		if (actor.kind == PayloadOutcomeKind::Shield)
		{
			address.contribution = actor.contribution;
			address.contribution_name = actor.contribution_name;
		}
		if (!frame.rule_context && uses_payload_outcome)
			address = *frame.hit_context;
		auto ship = owner(id, uses_payload_outcome, actor.ordnance);
		const auto *ability_state =
		    quote.abilities.contains(id) ? &quote.abilities.at(id) : ship.try_get<AbilityRuntime>();
		const auto ability_owner = op.officer.empty() ? op.equipment : op.officer;
		const auto ability_source = op.officer.empty() ? "equipment" : "officer";
		const auto ability_grant_id =
		    "temporary:" + source + "." + op.grant + ":" + ability_source + ":" + ability_owner;
		const bool removing_ability =
		    op.kind == EffectOperationKind::Remove && ability_state && ability_state->grants.contains(ability_grant_id);
		std::optional<CustomPropertyAddress> process_address;
		if (!actor.ordnance &&
		    (actor.kind != PayloadOutcomeKind::Shield || !op.equipment.empty() || !op.officer.empty() ||
		     !op.compartment.empty()) &&
		    (catalog_.process_grants.contains(op.effect) || op.kind == EffectOperationKind::Remove))
		{
			const auto candidate =
			    custom_property_address(property_actor(frame, actor, op.equipment, op.officer, op.compartment));
			const auto *runtime =
			    quote.processes.contains(candidate) ? &quote.processes.at(candidate) : process_runtime(candidate);
			if (catalog_.process_grants.contains(op.effect) ||
			    (runtime && std::any_of(runtime->processes.begin(), runtime->processes.end(),
			                            [&](const auto &entry)
			                            {
				                            return entry.second.source == source + "." + op.grant;
			                            })))
				process_address = candidate;
		}
		if (op.kind >= EffectOperationKind::StructuralRepair && op.kind <= EffectOperationKind::Recover)
		{
			ports_.native_outcome(quote, frame, op);
		}
		else if (op.kind == EffectOperationKind::HeatAdd || op.kind == EffectOperationKind::HeatRemove)
		{
			thermal(quote, frame, op);
		}
		else if (op.kind >= EffectOperationKind::PropertySet && op.kind <= EffectOperationKind::PropertyRate)
		{
			property(quote, frame, op, actor, current_operation);
		}
		else if (op.kind == EffectOperationKind::Damage)
		{
			if (op.damage < 0 || op.damage > 1000000)
				throw std::runtime_error("Effect damage outside supported bounds");
			if (uses_payload_outcome)
			{
				if (!ports_.hit_available(address))
					throw std::runtime_error("Damage outcome unavailable");
			}
			else if (!ship.has<CombatStructure>() && !ship.has<Destructible>())
				throw std::runtime_error("Damage target incompatible");
			if (op.damage)
				quote.damages.push_back({id, op.damage,
				                         uses_payload_outcome || actor.ordnance || !ship.has<CombatStructure>(),
				                         address, invocation});
		}
		else if (op.kind >= EffectOperationKind::StoreAdd)
		{
			if (actor.ordnance)
				throw std::runtime_error("Ordnance does not own a stock store");
			auto &destination = stock(id, op.store, uses_payload_outcome);
			bool valid = false;
			switch (op.kind)
			{
			case EffectOperationKind::StoreAdd:
				valid = quote.stocks.add(destination, op.resource, op.amount);
				break;
			case EffectOperationKind::StoreConsume:
				valid = quote.stocks.consume(destination, op.resource, op.amount);
				break;
			case EffectOperationKind::StoreSet:
				valid = quote.stocks.set(destination, op.resource, op.amount);
				break;
			case EffectOperationKind::StoreFill:
				valid = quote.stocks.fill(destination, op.resource);
				break;
			case EffectOperationKind::StoreTransfer:
				valid = quote.stocks.transfer(
				    stock(id == attacker ? protected_ship : attacker, op.from_store, id == attacker), destination,
				    op.resource, op.amount);
				break;
			default:
				break;
			}
			if (!valid)
				throw std::runtime_error("Store operation cannot satisfy exact stock/capacity commitment");
		}
		else if (process_address)
		{
			auto &runtime = processes(quote, *process_address);
			if (op.kind == EffectOperationKind::Remove)
				remove_process_grant(runtime, source + "." + op.grant);
			else
			{
				auto grant = catalog_.process_grants.at(op.effect);
				grant.native_id = ports_.process_binding(*process_address, grant);
				install_process_grant(runtime, catalog_.processes, grant, op.effect, source + "." + op.grant, op.grant,
				                      quote.tick, effect_invocation_context(frame, quote.tick));
			}
		}
		else if (catalog_.abilities.contains(op.effect) || removing_ability)
		{
			if (!ship.has<AbilityRuntime>())
				throw std::runtime_error("Incompatible ability grant target");
			quote.original_abilities.try_emplace(id, ship.get<AbilityRuntime>());
			auto &prepared = quote.abilities.try_emplace(id, ship.get<AbilityRuntime>()).first->second;
			ports_.ability(prepared, id, op, source + "." + op.grant);
		}
		else if (catalog_.periodic.contains(op.effect) ||
		         (op.kind == EffectOperationKind::Remove &&
		          ports_.active_exists(quote.active_effects ? &*quote.active_effects : nullptr, source + "." + op.grant,
		                               id)))
		{
			const auto context = address;
			auto &active = this->active(quote);
			periodic(active, quote.next_active, op, source + "." + op.grant, attacker, context, source,
			         original_equipment, quote.tick, frame.rule_context ? &*frame.rule_context : nullptr);
		}
		else if (op.kind == EffectOperationKind::Apply && catalog_.modifiers.contains(op.effect))
			modifier(quote, frame, ship, op,
			         op.equipment.empty() ? custom_modifier_target(property_actor(frame, actor, {}, op.officer))
			                              : op.equipment);
		else
		{
			const auto token = op.equipment.empty()
			                       ? custom_modifier_target(property_actor(frame, actor, {}, op.officer))
			                       : op.equipment;
			const auto &current_properties = quote.properties.contains(id)     ? quote.properties.at(id)
			                                 : ship.has<PropertyEffectOwner>() ? read_property_effects(ship)
			                                                                   : PropertyEffects{};
			const bool logical_modifier =
			    op.kind == EffectOperationKind::Remove &&
			    std::any_of(current_properties.applications.begin(), current_properties.applications.end(),
			                [&](const auto &application)
			                {
				                return application.source == source + "." + op.grant && application.equipment == token;
			                });
			if (logical_modifier || (op.kind == EffectOperationKind::Remove && !op.officer.empty()))
				op.equipment = token;
			const auto kind = (!op.equipment.empty() || logical_modifier) ? EffectApplicationKind::Modifier
			                                                              : EffectApplicationKind::Shield;
			ShieldComponent *shield = nullptr;
			PropertyEffects *property = nullptr;
			if (kind == EffectApplicationKind::Shield)
			{
				if (!ship.has<ShieldComponent>())
					throw std::runtime_error("Incompatible shield target");
				quote.original_shields.try_emplace(id, ship.get<ShieldComponent>());
				shield = &quote.shields.try_emplace(id, ship.get<ShieldComponent>()).first->second;
			}
			else
			{
				if (!ship.has<PropertyEffectOwner>())
					throw std::runtime_error("Incompatible modifier target");
				property = &properties(quote, ship);
			}
			application(ship,
			            {kind,
			             op.kind == EffectOperationKind::Remove ? EffectApplicationOperation::Remove
			                                                    : EffectApplicationOperation::Apply,
			             source + "." + op.grant, op.equipment, op.effect},
			            shield, property, quote.tick);
		}
	}
}
EffectPreparation::EffectPreparation(EffectPreparationCatalog catalog, EffectPreparationPorts ports, std::int64_t rate)
    : catalog_(catalog), ports_(std::move(ports)), rate_(rate)
{
}
flecs::entity EffectPreparation::require_target(EntityId id, bool ordnance) const
{
	const auto body = ports_.find(id, ordnance);
	if (ordnance)
	{
		PayloadHitContext context{id};
		context.ordnance = true;
		if (!body || !ports_.hit_available(context))
			throw std::runtime_error("Required ordnance target unavailable");
		return body;
	}
	if (!body || ports_.terminal(body))
		throw std::runtime_error("Required target unavailable");
	return body;
}

std::string EffectPreparation::equipment(const EffectGroupInvocation &frame, const std::string &binding) const
{
	if (binding.empty() || binding.front() != '$')
		return binding;
	if (binding == "$hit")
	{
		if (!frame.hit_context || !ports_.hit_available(*frame.hit_context))
			throw std::runtime_error("Required hit equipment unavailable");
		const auto &hit = *frame.hit_context;
		auto equipment = hit.kind == PayloadOutcomeKind::Shield
		                     ? "effect:" + std::to_string(hit.contribution) + ":" + hit.contribution_name
		                     : hit.equipment;
		if (equipment.empty())
			throw std::runtime_error("Hit does not identify equipment");
		return equipment;
	}
	const auto actor = effect_group_actor(frame, binding.substr(1));
	if (actor.kind == PayloadOutcomeKind::Shield && actor.contribution && !actor.contribution_name.empty())
		return "effect:" + std::to_string(actor.contribution) + ":" + actor.contribution_name;
	if (actor.equipment.empty())
		throw std::runtime_error("Required equipment binding unavailable");
	return actor.equipment;
}

RuleActor EffectPreparation::property_actor(const EffectGroupInvocation &frame, const RuleActor &original,
                                            const std::string &equipment, const std::string &officer,
                                            const std::string &compartment) const
{
	if ((!equipment.empty() + !officer.empty() + !compartment.empty()) > 1)
		throw std::runtime_error("Effect owner selectors conflict");
	auto actor = equipment.empty() && officer.empty() && compartment.empty() ? original : rule_body_actor(original);
	if (!equipment.empty())
	{
		actor.equipment = this->equipment(frame, equipment);
		actor.officer.clear();
		actor.compartment = -1;
	}
	if (!officer.empty())
	{
		actor.officer = officer;
		actor.equipment.clear();
		actor.compartment = -1;
	}
	if (!compartment.empty())
	{
		const auto index = ports_.compartment(actor.entity, compartment);
		if (!index)
			throw std::runtime_error("Effect compartment unavailable");
		actor = rule_body_actor(actor);
		actor.kind = PayloadOutcomeKind::Compartment;
		actor.compartment = *index;
	}
	return actor;
}

flecs::entity EffectPreparation::target(PreparedEffectGroup &quote, const EffectGroupInvocation &frame, EntityId id,
                                        bool uses_payload_outcome, bool ordnance) const
{
	// Guards carry their own ordnance identity; a later batch frame must not
	// reinterpret an earlier frame's same-number physical/ordnance target.
	ordnance = ordnance || (!frame.rule_context && uses_payload_outcome && frame.hit_context &&
	                        frame.hit_context->target == id && frame.hit_context->ordnance);
	quote.targets.push_back({id, uses_payload_outcome, ordnance});
	return require_target(id, ordnance);
}

StockStore &EffectPreparation::stock(PreparedEffectGroup &quote, const EffectGroupInvocation &frame, EntityId id,
                                     const std::string &name, bool uses_payload_outcome) const
{
	auto resolved = named_stock(target(quote, frame, id, uses_payload_outcome), ports_.definitions(id).logistics, name,
	                            quote.payments.contains(id) ? &quote.payments.at(id).network : nullptr);
	if (resolved.kind == NamedStockKind::Physical)
		quote.changed_inventories.insert(id);
	return resolved.stock;
}

PropertyEffects &EffectPreparation::properties(PreparedEffectGroup &quote, flecs::entity ship) const
{
	const auto id = ship.get<StableId>().id;
	auto original = quote.original_properties.find(id);
	if (original == quote.original_properties.end())
		original = quote.original_properties.emplace(id, read_property_effects(ship)).first;
	return quote.properties.try_emplace(id, original->second).first->second;
}

ProcessRuntime &EffectPreparation::processes(PreparedEffectGroup &quote, const CustomPropertyAddress &address) const
{
	const auto *runtime = process_runtime(address);
	if (!runtime)
		throw std::runtime_error("Process owner unavailable");
	quote.original_processes.try_emplace(address, *runtime);
	return quote.processes.try_emplace(address, *runtime).first->second;
}

std::vector<ActiveRuleEffect> &EffectPreparation::active(PreparedEffectGroup &quote) const
{
	if (!quote.active_effects)
	{
		quote.original_active_effects = ports_.active_snapshot();
		quote.active_effects = *quote.original_active_effects;
	}
	return *quote.active_effects;
}

const ProcessRuntime *EffectPreparation::process_runtime(const CustomPropertyAddress &address) const
{
	const auto body = ports_.find(address.entity, false);
	return body ? phoenix::process_runtime(body, address, ports_.definitions(address.entity).personnel) : nullptr;
}
void EffectPreparation::periodic(std::vector<ActiveRuleEffect> &prepared, std::uint64_t &next,
                                 const EffectOperation &operation, const std::string &source, EntityId original_source,
                                 const PayloadHitContext &context, const std::string &group_source,
                                 const std::string &original_equipment, std::int64_t tick,
                                 const NativeRuleContext *origin) const
{
	validate_effect_identity(source, {}, false);
	if (operation.kind == EffectOperationKind::Remove)
	{
		prepare_active_rule_grant(prepared, next, operation, source, original_source, context, group_source,
		                          original_equipment, nullptr, tick, origin);
		return;
	}
	if (!ports_.hit_available(context))
		throw std::runtime_error("Active rule target unavailable");
	prepare_active_rule_grant(prepared, next, operation, source, original_source, context, group_source,
	                          original_equipment, &catalog_.periodic.at(operation.effect), tick, origin);
}
void EffectPreparation::modifier(PreparedEffectGroup &quote, const EffectGroupInvocation &frame, flecs::entity ship,
                                 const EffectOperation &operation, const std::string &equipment) const
{
	if (!ship.has<PropertyEffectOwner>() || !ship.has<StableId>())
		throw std::runtime_error("Incompatible modifier owner");
	ModifierPreparationPort port;
	port.compatible = [&](const ModifierEffectDefinition &definition)
	{
		return ports_.modifier_compatible(ship, equipment, definition);
	};
	port.prepared = [&]() -> PropertyEffects &
	{
		return properties(quote, ship);
	};
	prepare_group_modifier(quote, frame, operation, equipment, catalog_.modifiers.at(operation.effect),
	                       catalog_.properties, port);
}
void EffectPreparation::application(flecs::entity ship, const EffectApplication &value, ShieldComponent *shields,
                                    PropertyEffects *properties, std::int64_t tick) const
{
	EffectApplicationTarget target;
	target.available = !ports_.terminal(ship);
	target.properties = ship.has<PropertyEffectOwner>();
	target.shields = ship.has<ShieldComponent>();
	target.modifier_compatible = [&](const std::string &equipment, const ModifierEffectDefinition &definition)
	{
		return ports_.modifier_compatible(ship, equipment, definition);
	};
	prepare_effect_application(value, shields, properties, tick,
	                           {catalog_.shields, catalog_.modifiers, catalog_.properties}, target);
}

ThermalEffectTarget EffectPreparation::thermal_target(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                                                      const EffectOperation &operation) const
{
	const auto actor = effect_group_actor(frame, operation.target);
	if (actor.ordnance)
		throw std::runtime_error("Ordnance has no native thermal owner");
	auto ship = target(quote, frame, actor.entity, false);
	const auto *power = ship.try_get<ShipResources>();
	if (!power)
		throw std::runtime_error("Thermal target has no resource network");
	auto &network = quote.payments.try_emplace(actor.entity, power->network).first->second.network;
	ResourceIndex selected = no_resource;
	if (!operation.store.empty())
	{
		const auto stock = named_stock(ship, ports_.definitions(actor.entity).logistics, operation.store, &network);
		if (stock.kind != NamedStockKind::Electrical)
			throw std::runtime_error("Thermal store must identify a native thermal reserve");
		selected = stock.index;
	}
	const auto equipment = operation.equipment.empty() ? actor.equipment : this->equipment(frame, operation.equipment);
	if (!equipment.empty())
	{
		const auto fitted = find_equipment(ship, equipment);
		if (!fitted || !fitted->resource_slot)
			throw std::runtime_error("Thermal equipment unavailable");
		const auto store = power->definition.systems.at(*fitted->resource_slot).store;
		if (store == no_resource || (selected != no_resource && selected != store))
			throw std::runtime_error("Thermal equipment and store disagree");
		selected = store;
	}
	if (selected == no_resource || selected >= network.stores().size())
		throw std::runtime_error("Thermal effect requires an explicit native equipment or store");
	return {actor.entity, selected};
}

void EffectPreparation::prepare_thermal_networks(PreparedEffectGroup &quote) const
{
	// Establish network copies before any stock alias is staged, including when
	// a preceding operation changes stock and a later operation changes heat.
	for (std::size_t i = 0; i < quote.invocations.size(); ++i)
	{
		quote.failed_invocation = i;
		const auto &frame = quote.invocations[i];
		for (const auto &operation : frame.execution.operations)
			if (operation.kind == EffectOperationKind::HeatAdd || operation.kind == EffectOperationKind::HeatRemove)
				(void)thermal_target(quote, frame, operation);
	}
}

void EffectPreparation::thermal(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                                const EffectOperation &operation) const
{
	if (operation.amount < 0 || operation.amount > resource_limit)
		throw std::runtime_error("Thermal amount outside supported bounds");
	const auto target = thermal_target(quote, frame, operation);
	auto &network = quote.payments.at(target.entity).network;
	if (operation.kind == EffectOperationKind::HeatAdd)
		(void)network.add_thermal(target.store, operation.amount);
	else
		(void)network.remove_thermal(target.store, operation.amount);
}
CustomPropertyState &EffectPreparation::custom_properties(PreparedEffectGroup &quote,
                                                          const CustomPropertyAddress &address) const
{
	const auto body = ports_.find(address.entity, false);
	const auto *current =
	    body ? phoenix::custom_property_state(body, address, ports_.definitions(address.entity).personnel) : nullptr;
	if (!current)
		throw std::runtime_error("Custom property owner unavailable");
	quote.original_custom_properties.try_emplace(address, *current);
	return quote.custom_properties.try_emplace(address, *current).first->second;
}
void EffectPreparation::property(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                                 const EffectOperation &op, const RuleActor &target, std::size_t index) const
{
	const auto address = custom_property_address(property_actor(frame, target, op.equipment, op.officer));
	auto &state = custom_properties(quote, address);
	const auto found = state.bases.find(op.property);
	if (found == state.bases.end())
		throw std::runtime_error("Custom property is not declared on target");
	const auto &definition = catalog_.properties.at(op.property);
	if (op.kind == EffectOperationKind::PropertySet)
	{
		read_custom_property_value(definition, custom_property_json(op.value), "property operation");
		found->second = op.value;
		return;
	}
	if (!std::holds_alternative<std::int64_t>(found->second))
		throw std::runtime_error("Numeric property required");
	auto amount = op.amount;
	if (op.kind == EffectOperationKind::PropertySubtract)
		amount = -amount;
	if (op.kind == EffectOperationKind::PropertyRate)
	{
		std::optional<std::string> key = frame.source + ":" + std::to_string(index) + ":" + op.property;
		if (frame.rule_context && frame.rule_context->owner_effect_kind != NativeEffectKind::None)
		{
			if (ports_.property_rate_owner_live(*frame.rule_context))
				*key += native_property_rate_suffix(*frame.rule_context);
			else
				key.reset();
		}
		const auto previous = key && state.remainders.contains(*key) ? state.remainders.at(*key) : 0;
		const auto numerator = amount + previous;
		amount = numerator / rate_;
		const auto remainder = numerator % rate_;
		if (key && remainder)
		{
			if (key->size() > 512 ||
			    (!state.remainders.contains(*key) && state.remainders.size() >= custom_property_rate_limit))
				throw std::runtime_error("Property rate identity capacity exhausted");
			state.remainders[*key] = remainder;
		}
		else if (key)
			state.remainders.erase(*key);
	}
	const auto base = std::get<std::int64_t>(found->second);
	const auto next = base + amount;
	if (!op.saturate && (next < definition.minimum || next > definition.maximum))
		throw std::runtime_error("Property operation exceeds declared bounds");
	found->second = std::clamp(next, definition.minimum, definition.maximum);
}
} // namespace phoenix
