module;
#include "native_headers.hpp"

module phoenix.simulation;
import :effect_work;
import phoenix.world.schedule.profiling;

namespace phoenix::world_detail
{
auto Runtime::process_control_address(EntityId ship, const ProcessControlDefinition &control) const
    -> CustomPropertyAddress
{
	CustomPropertyAddress owner{ship};
	if (control.source == "equipment")
		owner.equipment = control.owner;
	else if (control.source == "officer")
		owner.officer = control.owner;
	else if (control.source == "compartment")
	{
		const auto &combat = definition(ship).combat;
		if (!combat)
			throw std::runtime_error("Process compartment owner unavailable");
		const auto at = std::find_if(combat->compartments.begin(), combat->compartments.end(),
		                             [&](const auto &room)
		                             {
			                             return room.id == control.owner;
		                             });
		if (at == combat->compartments.end())
			throw std::runtime_error("Process compartment owner unavailable");
		owner.compartment = std::distance(combat->compartments.begin(), at);
	}
	return owner;
}

auto Runtime::process_control_grant(EntityId ship, const ProcessControlDefinition &control) const
    -> const ProcessState *
{
	const auto *runtime = process_runtime(process_control_address(ship, control));
	if (!runtime)
		return nullptr;
	const ProcessState *result = nullptr;
	for (const auto &[id, state] : runtime->processes)
		if (state.parent_process.empty() && state.slot == control.process &&
		    (!result || (!result->admitted && state.admitted)))
			result = &state;
	return result;
}

auto Runtime::process_channel_bound(EntityId ship, std::size_t controller, std::size_t channel) const -> bool
{
	for (const auto &binding : definition(ship).processes.controls)
		if (binding.controller == controller && binding.enabled == channel)
			return true;
	return false;
}

auto Runtime::process_arguments_shape(std::span<const std::int64_t> arguments) -> bool
{
	return arguments.empty() || (arguments.size() == 1 && arguments.front() > 0 && arguments.front() <= 1000000000);
}

auto Runtime::process_generation_valid(EntityId ship, std::size_t controller, std::size_t channel,
                                       std::span<const std::int64_t> arguments) const -> bool
{
	if (!process_arguments_shape(arguments))
		return false;
	if (arguments.empty())
		return true;
	for (const auto &binding : definition(ship).processes.controls)
		if (binding.controller == controller && binding.enabled == channel)
		{
			const auto *state = process_control_grant(ship, binding);
			return state && state->generation == static_cast<std::uint64_t>(arguments.front());
		}
	return false;
}

auto Runtime::process_channel_valid(EntityId ship, std::size_t controller, std::size_t channel,
                                    std::int64_t value) const -> bool
{
	for (const auto &binding : definition(ship).processes.controls)
		if (binding.controller == controller && binding.enabled == channel)
		{
			const auto *state = process_control_grant(ship, binding);
			return (value == 0 || value == 1) && state &&
			       (!value || state->admitted ||
			        process_conflict(process_control_address(ship, binding), *state).empty());
		}
	return true;
}

auto Runtime::process_command_valid(EntityId ship, std::size_t controller, std::size_t channel,
                                    const OperatingCommand &command) const -> bool
{
	return !process_channel_bound(ship, controller, channel) ||
	       (process_generation_valid(ship, controller, channel, command.arguments) &&
	        process_channel_valid(ship, controller, channel, command.value));
}

auto Runtime::consume_process_commands(EntityId ship) -> void
{
	auto &controllers = entities.at(ship).get_mut<EntityControls>().controllers;
	for (const auto &binding : definition(ship).processes.controls)
	{
		auto &channel = controllers.at(binding.controller).commands.at(binding.enabled);
		if (channel.revision == channel.consumed)
			continue;
		channel.consumed = channel.revision;
		const auto *state = process_control_grant(ship, binding);
		channel.accepted = state &&
		                   process_generation_valid(ship, binding.controller, binding.enabled, channel.arguments) &&
		                   process_channel_valid(ship, binding.controller, binding.enabled, channel.value);
		if (channel.accepted)
			channel.accepted = process_request(process_control_address(ship, binding), state->id, channel.value != 0);
	}
}

auto Runtime::process_controls(const ConsoleAddress &address, const ConsoleControllerRuntime &controller,
                               std::size_t channel, Json &buttons) const -> bool
{
	const auto &controllers = entities.at(address.ship).get<EntityControls>().controllers;
	for (const auto &binding : definition(address.ship).processes.controls)
	{
		if (&controller != &controllers.at(binding.controller) || controller.definition.console != address.console ||
		    binding.enabled != channel)
			continue;
		const auto *state = process_control_grant(address.ship, binding);
		if (!state)
			return true;
		const auto &spec = controller.definition.commands.at(channel);
		OperatingCommand command{address.ship,
		                         std::string(operating_action(spec.action).name),
		                         state->requested ? 0 : 1,
		                         {},
		                         controller.definition.id,
		                         spec.id,
		                         controller.generation};
		command.arguments = {static_cast<std::int64_t>(state->generation)};
		auto button = encode_operating_command(command);
		button["id"] = "process." + binding.id + "." + std::to_string(command.value);
		button["label"] = state->requested ? "Stop process" : "Start process";
		button["process_label"] = process_definition(*state).label;
		buttons.push_back(std::move(button));
		return true;
	}
	return false;
}

auto Runtime::process_control_visible(const ConsoleAddress &address, const ProcessControlDefinition &binding) const
    -> bool
{
	return entities.at(address.ship).get<EntityControls>().controllers.at(binding.controller).definition.console ==
	       address.console;
}

auto Runtime::process_rule_visible(const ConsoleAddress &address, const NativeRuleSource &source) const -> bool
{
	// Equipment feeds already permit every native process on that equipment.
	const auto *console = seat(address.ship, address.console);
	if (console && !source.owner.equipment.empty() &&
	    std::find(console->feed_systems.begin(), console->feed_systems.end(), source.owner.equipment) !=
	        console->feed_systems.end())
		return true;
	for (const auto &binding : definition(address.ship).processes.controls)
		if (process_control_visible(address, binding))
		{
			const auto owner = process_control_address(address.ship, binding);
			if (process_actor(owner) != source.owner)
				continue;
			if (const auto *state = process_control_grant(address.ship, binding))
				if (source.key == process_run_source(owner, *state) ||
				    source.key == "process-grant:" + process_instance_key(owner, *state))
					return true;
		}
	return false;
}

auto Runtime::process_job_details_visible(const ConsoleAddress &address, const CustomPropertyAddress &owner,
                                          const ProcessState &state, const Observation *permitted) const -> bool
{
	return phoenix::process_job_details_visible(address, owner, state, process_definition(state), permitted);
}

auto Runtime::capture_process_rows(const ConsoleAddress &address, const DecisionObservation *decision,
                                   const Observation *permitted) const -> std::vector<DecisionProcessRow>
{
	TickWorkProfile profile("projection.processes");
	std::vector<DecisionProcessRow> rows;
	const auto details_visible = [&](const CustomPropertyAddress &owner, const ProcessState &state)
	{
		if (!decision)
			return process_job_details_visible(address, owner, state, permitted);
		const auto native = process_definition(state).native;
		if (native == NativeProcessKind::Effects || state.target.empty())
			return true;
		if (native == NativeProcessKind::Ftl)
		{
			for (const auto &row : decision->ftl)
				if (row.system == owner.equipment)
					return true;
			return false;
		}
		if (native == NativeProcessKind::ServicingEnergy || native == NativeProcessKind::ServicingAmmunition)
		{
			for (const auto &row : decision->craft)
				if (row.id == std::to_string(state.origin.target.entity) && row.bay == owner.equipment &&
				    row.phase == "docked")
					return true;
			return false;
		}
		if (state.origin.target.entity != address.ship)
			return false;
		if (native == NativeProcessKind::Repair)
		{
			for (const auto &row : decision->teams)
				if (row.id == owner.equipment && row.repair)
					return true;
			return false;
		}
		if (native == NativeProcessKind::Medical || native == NativeProcessKind::Recovery)
		{
			const auto &target = state.origin.target;
			const auto &patient = target.officer.empty() ? target.equipment : target.officer;
			if (patient.empty())
				return false;
			for (const auto &team : decision->teams)
				for (const auto &row : team.patients)
					if (row.id == patient || row.id == "team:" + patient)
						return true;
			for (const auto &row : decision->personnel.officers)
				if (row.id == patient)
					return true;
		}
		return false;
	};
	std::set<std::string> published;
	const auto append = [&](const CustomPropertyAddress &owner, const ProcessState &state, const std::string &endpoint)
	{
		if (rows.size() >= 4096)
			return;
		const auto *parent = state.parent_process.empty() ? nullptr : process_state(owner, state.parent_process);
		if (!state.parent_process.empty() && !parent)
			return;
		const auto &spec = process_definition(state);
		const auto key = process_instance_key(owner, state);
		const bool driver = process_is_native_driver(spec, state);
		const bool details = !driver && details_visible(owner, state);
		const bool hidden_job = process_has_finite_work(spec) && !details && !driver;
		published.insert(key);
		DecisionProcessRow row;
		row.id = key + (endpoint.empty() ? "" : ":" + endpoint);
		row.control = endpoint;
		row.process = state.definition;
		row.slot = state.slot;
		row.label = spec.label;
		row.source = process_owner_kind(owner);
		row.owner = process_owner_name(owner);
		row.requested = state.requested;
		row.phase = hidden_job ? (state.requested ? "paused" : "off") : state.phase;
		row.blocker = hidden_job ? "Job details unavailable" : state.blocker;
		row.generation = state.generation;
		row.run = state.run;
		row.output = hidden_job ? 0 : state.work_output;
		row.upkeep_output = state.idle_output;
		row.work = hidden_job ? 0 : state.actual_work;
		row.work_unit = spec.work_unit;
		row.display_scale = spec.work_divisor;
		row.finite = process_has_finite_work(spec);
		row.driver = driver;
		row.parent = state.parent_process.empty() ? std::string{} : process_instance_key(owner, *parent);
		row.details_available = details;
		row.target = details ? state.target : std::string{};
		row.progress = details ? state.progress : 0;
		row.required_work = details ? state.required_work : 0;
		row.completed = details && state.completed;
		row.ready = details && state.ready;
		row.completion_delivered = details && state.completed;
		row.properties = capture_execution_properties(process_actor(owner), spec.observations);
		rows.push_back(std::move(row));
	};
	for (const auto &binding : definition(address.ship).processes.controls)
		if (process_control_visible(address, binding))
			if (const auto *state = process_control_grant(address.ship, binding))
			{
				const auto owner = process_control_address(address.ship, binding);
				append(owner, *state, binding.id);
				if (const auto *runtime = process_runtime(owner))
					for (const auto &[id, child] : runtime->processes)
						if (child.parent_process == state->id &&
						    !published.contains(process_instance_key(owner, child)) && details_visible(owner, child))
							append(owner, child, {});
			}
	// Native System readouts already express equipment-local visibility. Do not
	// require extra process endpoints just to preserve ordinary machinery panels.
	const auto append_system = [&](const auto &system)
	{
		const CustomPropertyAddress owner{address.ship, system.id};
		if (const auto *runtime = process_runtime(owner))
			for (const auto &[id, state] : runtime->processes)
				if (!published.contains(process_instance_key(owner, state)) &&
				    (state.parent_process.empty() || details_visible(owner, state)))
					append(owner, state, {});
	};
	if (decision)
	{
		for (const auto &system : decision->systems)
			append_system(system);
	}
	else if (permitted)
	{
		for (const auto &system : permitted->systems)
			append_system(system);
	}
	// Facilities are entity-owned rather than fitted equipment. A permitted
	// patient readout grants the corresponding recovery episode, not unrelated
	// entity processes or another department's patients.
	const CustomPropertyAddress entity_owner{address.ship};
	if (decision || permitted)
		if (const auto *runtime = process_runtime(entity_owner))
			for (const auto &[id, state] : runtime->processes)
				if (process_definition(state).native == NativeProcessKind::Recovery && !state.parent_process.empty() &&
				    details_visible(entity_owner, state))
				{
					if (const auto *driver = process_state(entity_owner, state.parent_process);
					    driver && !published.contains(process_instance_key(entity_owner, *driver)))
						append(entity_owner, *driver, {});
					if (!published.contains(process_instance_key(entity_owner, state)))
						append(entity_owner, state, {});
				}
	return rows;
}
auto Runtime::process_board(const ConsoleAddress &address, const Observation *permitted) const -> Json
{
	Json rows = Json::array();
	for (const auto &row : capture_process_rows(address, nullptr, permitted))
	{
		rows.push_back({{"id", row.id},
		                {"control", row.control},
		                {"process", row.process},
		                {"slot", row.slot},
		                {"label", row.label},
		                {"source", row.source},
		                {"owner", row.owner},
		                {"requested", row.requested},
		                {"phase", row.phase},
		                {"blocker", row.blocker},
		                {"generation", row.generation},
		                {"run", row.run},
		                {"output", row.output},
		                {"upkeep_output", row.upkeep_output},
		                {"work", row.work},
		                {"work_unit", row.work_unit},
		                {"display_scale", row.display_scale},
		                {"finite", row.finite},
		                {"driver", row.driver},
		                {"parent", row.parent},
		                {"details_available", row.details_available},
		                {"target", row.target},
		                {"progress", row.progress},
		                {"required_work", row.required_work},
		                {"completed", row.completed},
		                {"ready", row.ready},
		                {"completion_delivered", row.completion_delivered},
		                {"properties", encode_decision_properties(row.properties)}});
	}
	return rows;
}
auto Runtime::capture_machinery_decision(const ConsoleAddress &address, const DecisionObservation &permitted) const
    -> MachineryDecisionSnapshot
{
	MachineryDecisionSnapshot snapshot;
	snapshot.abilities = capture_ability_decision(address, permitted.contacts, permitted.bearing_contacts);
	for (const auto &row : capture_process_rows(address, &permitted, nullptr))
		if (!row.control.empty())
			snapshot.processes[row.control] = process_decision_facts(row);
	return snapshot;
}
} // namespace phoenix::world_detail
