module;
#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

module phoenix.control.execution;

namespace phoenix
{
void append_controller_workflow_facts(const ConsoleControllerRuntime &controller,
                                      const ControllerObservationSources &input, std::vector<state_tree::Value> &values)
{
	if (!controller.brain->workflow_base)
		return;
	using state_tree::Value;
	const auto base = *controller.brain->workflow_base;
	for (std::size_t i = 0; i < 5; ++i)
		values.at(base + i) = Value::string(i == 4 ? "free" : "");
	for (std::size_t i = 5; i < 10; ++i)
		values.at(base + i) = Value::integer(0);
	for (std::size_t i = 10; i < 14; ++i)
		values.at(base + i) = Value::boolean(false);
	values.at(base + 14) = Value::integer(2000000000);
	values.at(base + 15) = Value::boolean(false);
	const auto &observed = input.observation;
	values.at(base + 7) = Value::integer(observed.gameplay.value("alert_maximum", 0LL));
	const auto owns = [&](const std::string &operation, const std::string &equipment)
	{
		return controller_operation_owns(controller, operation, equipment);
	};
	const auto docking = observed.gameplay.find("docking");
	const auto owns_port = [&](const Json &id)
	{
		if (!controller.definition.operations.contains("docking") || docking == observed.gameplay.end() ||
		    !docking->contains("ports"))
			return false;
		for (const auto &port : docking->at("ports"))
			if (port.at("id") == id && port.at("controller") == controller.definition.id)
				return true;
		return false;
	};
	if (docking != observed.gameplay.end() && docking->contains("ports"))
	{
		values.at(base + 6) = Value::integer(docking->at("revision"));
		for (const auto &port : docking->at("ports"))
		{
			if (owns_port(port.at("id")))
				values.at(base + 2) = Value::string(port.at("id"));
			values.at(base + 4) = Value::string(port.at("phase"));
			if (port.at("phase") != "free")
				break;
		}
	}
	// The scenario offers jobs explicitly. Hidden, unoffered and finished jobs
	// cannot become automatic boarding orders. Manifest rows are the Console's
	// ordinary permitted projection, not access to away-tree private memory.
	const Json *mission = nullptr;
	if (const auto rows = observed.gameplay.find("away_missions"); rows != observed.gameplay.end())
		for (const auto &row : *rows)
		{
			const auto phase = row.at("phase").get<std::string>();
			if (!row.value("ai_dispatch", false) || phase == "failed" || phase == "lost" || phase == "recovered" ||
			    (phase == "available" && !owns("mission", row.value("dispatch_system", std::string{}))))
				continue;
			mission = &row;
			break;
		}
	if (mission)
	{
		values.at(base) = Value::string(mission->at("id"));
		values.at(base + 1) = Value::string(mission->at("phase"));
		values.at(base + 5) = Value::integer(mission->at("revision"));
		values.at(base + 10) = Value::boolean(mission->value("work_complete", false));
		values.at(base + 11) = Value::boolean(mission->value("launch_blocker", std::string{}).empty());
		values.at(base + 15) = Value::boolean(mission->value("transport", std::string{}) == "ship");
		for (const auto &contact : observed.contacts)
			if (contact.id == mission->at("target") && mission->value("transport", std::string{}) == "ship")
				values.at(base + 14) =
				    Value::integer(fixed::length({contact.x - observed.own.x, contact.y - observed.own.y}));
		if (docking != observed.gameplay.end() && docking->contains("targets"))
			for (const auto &target : docking->at("targets"))
				if (target.at("entity") == mission->at("target") &&
				    std::find(mission->at("docking_ports").begin(), mission->at("docking_ports").end(),
				              target.at("port")) != mission->at("docking_ports").end())
					for (const auto &local : target.at("local_ports"))
						if (owns_port(local))
						{
							values.at(base + 2) = Value::string(local);
							values.at(base + 3) = Value::string(std::to_string(target.at("entity").get<EntityId>()) +
							                                    "/" + target.at("port").get<std::string>());
							break;
						}
	}
	// Travel facts use accepted same-Console navigation and observed motion.
	// An impulse Controller cannot read another Console's private policy.
	for (std::size_t i = 0; i < input.controls.controllers.size(); ++i)
	{
		const auto &owner = input.controls.controllers[i];
		if (owner.definition.console != controller.definition.console)
			continue;
		for (std::size_t j = 0; j < owner.definition.commands.size(); ++j)
			if (owner.definition.commands[j].action == OperatingAction::Destination)
			{
				const auto &command = input.frame.commands().contains(input.entity)
				                          ? input.frame.commands().at(input.entity).at(i).at(j)
				                          : owner.commands[j];
				if (!command.value || command.arguments.size() != 2)
					continue;
				const fixed::Vec delta{command.arguments[0] - observed.own.x, command.arguments[1] - observed.own.y};
				values.at(base + 8) = Value::integer(fixed::length(delta));
				auto angle = (fixed::bearing(delta) - observed.own.heading + 540000) % 360000 - 180000;
				values.at(base + 12) = Value::boolean(angle >= -5000 && angle <= 5000);
				values.at(base + 13) = Value::boolean(true);
			}
	}
	std::int64_t deceleration = 1;
	if (docking != observed.gameplay.end() && docking->contains("guidance"))
	{
		const auto &guidance = docking->at("guidance");
		const fixed::Vec delta{guidance.at("x").get<std::int64_t>() - observed.own.x,
		                       guidance.at("y").get<std::int64_t>() - observed.own.y};
		values.at(base + 8) = Value::integer(fixed::length(delta));
		const auto angle = (fixed::bearing(delta) - observed.own.heading + 540000) % 360000 - 180000;
		values.at(base + 12) = Value::boolean(angle >= -5000 && angle <= 5000);
		values.at(base + 13) = Value::boolean(guidance.at("navigate").get<bool>());
	}
	for (const auto &system : observed.systems)
		if (system.kind == OperatingSystem::Engine && system.telemetry.contains("deceleration"))
			deceleration = std::max(deceleration, system.telemetry.at("deceleration"));
	const auto speed = fixed::length({observed.own.vx, observed.own.vy});
	values.at(base + 9) = Value::integer(speed * speed / (2 * deceleration) + 250000);
}
} // namespace phoenix
