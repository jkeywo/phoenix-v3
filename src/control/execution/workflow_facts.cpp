module;
#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.execution;

namespace phoenix
{
void append_controller_workflow_facts(const ConsoleControllerRuntime &controller,
                                      const ControllerObservationSources &input, std::vector<state_tree::Value> &values)
{
	append_controller_medical_facts(controller, input, values);
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
	values.at(base + 16) = Value::boolean(false);
	values.at(base + 17) = Value::boolean(false);
	values.at(base + 18) = Value::string("");
	values.at(base + 19) = Value::string("");
	values.at(base + 20) = Value::integer(0);
	values.at(base + 21) = Value::boolean(false);
	const auto &observed = input.observation;
	// Retired tactical workflow slots remain neutral for compatible authored
	// factual observers. They cannot select a hostile, manoeuvre or fire policy.
	for (std::size_t i = 22; i <= 30; ++i)
		values.at(base + i) =
		    i == 22 || i == 23 || i == 24 || i == 29 || i == 30 ? Value::boolean(false) : Value::integer(0);
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
			if (mission)
			{
				mission = nullptr;
				break;
			}
			mission = &row;
		}
	if (mission)
	{
		values.at(base + 23) =
		    Value::boolean(mission->at("phase") == "deployed" && !mission->value("work_complete", false));
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
	// The original active objective retains the disclosed destination and cargo
	// instruction through interruptions. A radar return is needed to dock, not
	// to navigate toward its charted location.
	if (!mission)
		if (const auto *objective = controller_navigation_objective(input, controller))
		{
			const auto schema = objective->value.schema;
			const auto &fields = objective->value.fields;
			const auto key = [&](const char *name)
			{
				return input.definitions.field(schema, name);
			};
			if (fields.contains(key("delivery_target")))
			{
				const auto target = std::get<std::int64_t>(fields.at(key("delivery_target")));
				const auto port = std::get<std::string>(fields.at(key("delivery_port")));
				values.at(base + 16) = Value::boolean(true);
				values.at(base + 18) = Value::string(std::get<std::string>(fields.at(key("delivery_source"))));
				values.at(base + 19) = Value::string(
				    std::to_string(target) + "/" + std::get<std::string>(fields.at(key("delivery_store"))) + "/" +
				    std::get<std::string>(fields.at(key("delivery_material"))) + "/" +
				    std::to_string(std::get<std::int64_t>(fields.at(key("delivery_quantity")))));
				values.at(base + 20) = Value::integer(objective->value.subject);
				if (docking != observed.gameplay.end() && docking->contains("ports"))
					for (const auto &local : docking->at("ports"))
						if (local.at("target") == target && local.at("phase") != "free" &&
						    local.at("phase") != "separating")
							values.at(base + 21) = Value::boolean(true);
				if (docking != observed.gameplay.end() && docking->contains("targets"))
					for (const auto &row : docking->at("targets"))
						if (row.at("entity") == target && row.at("port") == port)
						{
							// Every Controller on this Console can observe the compatible
							// handover; only the port owner can submit the dock request.
							values.at(base + 17) = Value::boolean(!row.at("local_ports").empty());
							if (!row.at("local_ports").empty())
								values.at(base + 21) = Value::boolean(true);
							for (const auto &local : row.at("local_ports"))
								if (owns_port(local))
								{
									values.at(base + 2) = Value::string(local);
									values.at(base + 3) = Value::string(std::to_string(target) + "/" + port);
									break;
								}
						}
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
