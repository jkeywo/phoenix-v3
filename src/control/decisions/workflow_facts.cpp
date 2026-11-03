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
	values.at(base + 7) = Value::integer(observed.authority.alert_maximum);
	const auto owns = [&](const std::string &operation, const std::string &equipment)
	{
		return controller_operation_owns(controller, operation, equipment);
	};
	const auto &docking = observed.docking;
	const auto owns_port = [&](const std::string &id)
	{
		if (!controller.definition.operations.contains("docking"))
			return false;
		for (const auto &port : docking.ports)
			if (port.id == id && port.controller == controller.definition.id)
				return true;
		return false;
	};
	if (docking.observed)
	{
		values.at(base + 6) = Value::integer(docking.revision);
		const DockingPortDecision *active = nullptr;
		for (const auto &port : docking.ports)
			if (port.phase != "free")
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
			values.at(base + 4) = Value::string(active->phase);
			if (owns_port(active->id))
				values.at(base + 2) = Value::string(active->id);
		}
	}
	// The scenario offers jobs explicitly. Hidden, unoffered and finished jobs
	// cannot become automatic boarding orders. Manifest rows are the Console's
	// ordinary permitted projection, not access to away-tree private memory.
	const AwayMissionDecision *mission = nullptr;
	for (const auto &row : observed.away_missions)
	{
		const auto phase = row.phase;
		if (!row.ai_dispatch || phase == "failed" || phase == "lost" || phase == "recovered" ||
		    (phase == "available" && !owns("mission", row.dispatch_system)))
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
		values.at(base + 23) = Value::boolean(mission->phase == "deployed" && !mission->work_complete);
		values.at(base) = Value::string(mission->id);
		values.at(base + 1) = Value::string(mission->phase);
		values.at(base + 5) = Value::integer(mission->revision);
		values.at(base + 10) = Value::boolean(mission->work_complete);
		values.at(base + 11) = Value::boolean(mission->launch_blocker.empty());
		values.at(base + 15) = Value::boolean(mission->transport == "ship");
		for (const auto &contact : observed.contacts)
			if (contact.id == mission->target && mission->transport == "ship")
				values.at(base + 14) =
				    Value::integer(fixed::length({contact.x - observed.own.x, contact.y - observed.own.y}));
	}
	// New docking and cargo choices are per-objective candidates. These legacy
	// factual slots cannot choose an offered port, delivery or transfer instruction.
	// Travel geometry requires explicit accepted command reads. Controller order
	// and unrelated same-Console command state cannot choose the destination.
	const auto fact = [&](std::size_t field) -> const Value *
	{
		const auto slot = controller.brain->observation_layout->travel.at(field);
		return slot ? &values.at(*slot) : nullptr;
	};
	const auto *active = fact(0), *accepted = fact(1), *x = fact(2), *y = fact(3);
	const auto aligned = [&](std::int64_t desired)
	{
		const auto angle = (desired - observed.own.heading + 540000) % 360000 - 180000;
		return angle >= -5000 && angle <= 5000;
	};
	const bool has_course = active && accepted && x && y && active->number && accepted->number;
	if (has_course)
	{
		const fixed::Vec delta{x->number - observed.own.x, y->number - observed.own.y};
		values.at(base + 8) = Value::integer(fixed::length(delta));
		values.at(base + 12) = Value::boolean(aligned(fixed::bearing(delta)));
		values.at(base + 13) = Value::boolean(true);
	}
	std::int64_t deceleration = 1;
	if (docking.guidance)
	{
		const auto &guidance = *docking.guidance;
		const fixed::Vec delta{guidance.x - observed.own.x, guidance.y - observed.own.y};
		values.at(base + 8) = Value::integer(fixed::length(delta));
		// Physical docking guidance cannot mask a turn required by a newly accepted course.
		values.at(base + 12) =
		    Value::boolean(aligned(fixed::bearing(delta)) && (!has_course || values.at(base + 12).number));
		values.at(base + 13) = Value::boolean(guidance.navigate);
	}
	for (const auto &system : observed.systems)
		if (system.kind == OperatingSystem::Engine && system.telemetry.contains(TelemetryField::deceleration))
			deceleration = std::max(deceleration, system.telemetry.at(TelemetryField::deceleration));
	const auto speed = fixed::length({observed.own.vx, observed.own.vy});
	values.at(base + 9) = Value::integer(speed * speed / (2 * deceleration) + 250000);
}
} // namespace phoenix
