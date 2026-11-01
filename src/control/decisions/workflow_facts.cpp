module;
#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.decisions;

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
		const Json *active = nullptr;
		for (const auto &port : docking->at("ports"))
			if (port.at("phase") != "free")
			{
				if (active)
				{
					active = nullptr;
					values.at(base + 4) = Value::string("multiple");
					break;
				}
				active = &port;
			}
		if (active)
		{
			values.at(base + 4) = Value::string(active->at("phase"));
			if (owns_port(active->at("id")))
				values.at(base + 2) = Value::string(active->at("id"));
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
	}
	// New docking and cargo choices are per-objective candidates. These legacy
	// factual slots cannot choose an offered port, delivery or transfer instruction.
	// Travel geometry requires explicit accepted command reads. Controller order
	// and unrelated same-Console command state cannot choose the destination.
	const auto fact = [&](const std::string &name) -> const Value *
	{
		const auto &registry = controller.brain->program.registry.observations;
		for (std::size_t i = 0; i < registry.size(); ++i)
			if (registry[i].name == name)
				return &values[i];
		return nullptr;
	};
	const auto *active = fact("travel.active"), *accepted = fact("travel.accepted"), *x = fact("travel.x"),
	           *y = fact("travel.y");
	if (active && accepted && x && y && active->number && accepted->number)
	{
		const fixed::Vec delta{x->number - observed.own.x, y->number - observed.own.y};
		values.at(base + 8) = Value::integer(fixed::length(delta));
		const auto angle = (fixed::bearing(delta) - observed.own.heading + 540000) % 360000 - 180000;
		values.at(base + 12) = Value::boolean(angle >= -5000 && angle <= 5000);
		values.at(base + 13) = Value::boolean(true);
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
