module;
#include "native_headers.hpp"

module phoenix.simulation;
import :command_routing;

namespace phoenix::world_detail
{
auto Runtime::controller_decision_facts(const ConsoleControllerRuntime &controller, const Observation &observed,
                                        std::vector<state_tree::Value> &values) const -> void
{
	ControllerDecisionContext context;
	context.tick = ecs.get<Clock>().tick;
	const auto entity = entities.at(observed.own.id);
	const auto &data = definition(observed.own.id);
	if (const auto *craft = entity.try_get<CraftState>();
	    craft && (craft->order == "defend" || craft->order == "escort"))
	{
		context.escort = true;
		context.escort_anchor = craft->target == craft->home ? craft->rendezvous : craft->target_position;
		context.engagement_range = data.craft->engagement_range;
	}
	if (data.craft)
		context.altitude_offset = data.craft->cruise_altitude + craft_flight_band(observed.own.id);
	if (data.combat)
		for (const auto &[system, compartment] : data.combat->equipment_compartments)
			context.repair_system_indices.emplace(system,
			                                      static_cast<std::int64_t>(context.repair_system_indices.size()) + 1);
	for (const auto &command : controller.definition.commands)
		for (const auto consumer : command.consumers)
		{
			const auto child = entity.get<EquipmentIndex>().values.at(consumer);
			context.owned_systems.insert(child.get<Equipment>().id);
			if (command.action == OperatingAction::Speed)
				if (const auto *flight = child.try_get<FlightRuntime>())
					context.deceleration = std::max(context.deceleration, flight->definition.deceleration);
		}
	for (auto child : entity.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>();
		    m && m->definition.kind == OperatingSystem::Weapon && m->definition.get<WeaponDefinition>().point_defence &&
		    child.get<Equipment>().enabled &&
		    system_owner(observed.own.id, m->definition.id) ==
		        ConsoleAddress{observed.own.id, controller.definition.console} &&
		    !structurally_disabled(entity, m->definition.id))
			context.point_defence_target = std::max<std::int64_t>(
			    context.point_defence_target, point_defence_target(entity, m->definition, observed.contacts));
	for (const auto &weapon : combat_observation(observed).at("weapons"))
		if (context.owned_systems.contains(weapon.at("id").get<std::string>()) &&
		    weapon.value("point_defence", false) &&
		    directed_targeting(observed.own.id, weapon.at("id").get<std::string>()) == DirectedTargetingKind::Intercept)
			context.intercepting_weapons.insert(weapon.at("id").get<std::string>());
	phoenix::controller_decision_facts(controller, observed, context, values);
}

auto Runtime::controller_action_ports(EntityId id, std::size_t index, std::span<const state_tree::Value> source,
                                      std::optional<Observation> &fallback) -> ControllerActionPorts
{
	return {[this, id, index, source, &fallback]() -> const Observation &
	        {
		        const auto &controller = entities.at(id).get<EntityControls>().controllers.at(index);
		        const auto &brain = *controller.brain;
		        const ConsoleAddress address{
		            static_cast<EntityId>(source[brain.own[ControllerOwnField::Entity]].number),
		            controller.definition.console};
		        if (decision_frame.observations().contains(address))
			        return decision_frame.observations().at(address);
		        fallback = capture_decision_observation(address);
		        return *fallback;
	        },
	        [this, id, index](const state_tree::Output &output, std::span<const state_tree::Value> observations,
	                          const state_tree::Program *program)
	        {
		        controller_output(id, index, output, observations, program);
	        }};
}

auto Runtime::cancel_controller_action(EntityId id, std::size_t index, const std::string &reason,
                                       std::span<const state_tree::Value> source) -> void
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	std::optional<Observation> fallback;
	const auto ports = controller_action_ports(id, index, source, fallback);
	phoenix::cancel_controller_action(controller, reason, source, ports);
}

auto Runtime::select_controller_action(EntityId id, std::size_t index, const std::string &name,
                                       const std::string &invocation, std::span<const state_tree::Value> inputs,
                                       std::span<const state_tree::Value> source, std::uint64_t generation) -> bool
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	std::optional<Observation> fallback;
	const auto ports = controller_action_ports(id, index, source, fallback);
	return phoenix::select_controller_action(controller, name, invocation, inputs, source, generation, ports);
}

auto Runtime::run_controller_action(EntityId id, std::size_t index, std::span<const state_tree::Value> source) -> void
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	std::optional<Observation> fallback;
	const auto ports = controller_action_ports(id, index, source, fallback);
	phoenix::run_controller_action(controller, source, ports);
}

auto Runtime::admit_controller_decision(const ControllerDecisionProposal &proposal,
                                        std::span<const state_tree::Value> observations) -> bool
{
	auto entity = entities.find(proposal.address.entity);
	if (entity == entities.end() || !entity->second.has<EntityControls>())
		return false;
	auto &controllers = entity->second.get_mut<EntityControls>().controllers;
	for (std::size_t index = 0; index < controllers.size(); ++index)
	{
		auto &controller = controllers[index];
		if (controller.definition.console != proposal.address.console ||
		    controller.definition.id != proposal.address.controller)
			continue;
		if (controller.human || controller.generation != proposal.generation)
			return false;
		if (proposal.operation == "select")
			return select_controller_action(proposal.address.entity, index, proposal.action, proposal.invocation,
			                                proposal.inputs, observations, proposal.generation);
		if (proposal.operation == "continue")
			return true;
		if (proposal.operation == "cancel")
		{
			cancel_controller_action(proposal.address.entity, index, proposal.invocation, observations);
			return true;
		}
		if (proposal.operation.starts_with("coordination."))
			return controller_coordination_action(proposal.address.entity, index, proposal.operation, proposal.inputs);
		return false;
	}
	return false;
}
} // namespace phoenix::world_detail
