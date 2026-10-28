module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
module phoenix.gameplay.psychic.contributions;
namespace phoenix
{
PsychicTargetPort::~PsychicTargetPort() = default;
const AbilityGrantState *find_psychic_execution(const AbilityRuntime *runtime, const PsychicContribution &contribution)
{
	if (!runtime)
		return nullptr;
	const auto at = runtime->grants.find(contribution.grant);
	const auto *execution = at == runtime->grants.end() ? nullptr : &at->second;
	return execution && execution->definition == contribution.ability &&
	               execution->activation == contribution.activation
	           ? execution
	           : nullptr;
}
void refresh_psychic_contribution_work(PsychicChoirState &state, const AbilityRuntime *runtime,
                                       const PsychicDefinition &definition, bool source_departed)
{
	for (auto &[grant, contribution] : state.contributions)
	{
		const auto *execution = find_psychic_execution(runtime, contribution);
		contribution.work = execution && execution->phase == "active" && !source_departed
		                        ? definition.power(contribution.ability).strength * execution->funding_permille / 1000 *
		                              execution->fault_permille / 1000
		                        : 0;
		if (execution)
		{
			contribution.aim_x = execution->aim_x;
			contribution.aim_y = execution->aim_y;
		}
	}
}
void reconcile_psychic_contributions(PsychicChoirState &state, EntityId ship, const std::string &apparatus,
                                     const AbilityRuntime *runtime, const PsychicDefinition &definition,
                                     const PsychicTargetPort &targets)
{
	auto &contributions = state.contributions;
	std::erase_if(contributions,
	              [&](const auto &entry)
	              {
		              const auto *execution = find_psychic_execution(runtime, entry.second);
		              return !execution || execution->apparatus != apparatus ||
		                     (execution->phase != "active" &&
		                      !(execution->phase == "paused" && execution->resume_phase == "active"));
	              });
	if (!runtime)
		return;
	for (const auto &[grant, execution] : runtime->grants)
	{
		if (execution.apparatus != apparatus || execution.phase != "active" ||
		    !definition.has_power(execution.definition))
			continue;
		auto found = contributions.find(grant);
		if (found != contributions.end() && found->second.activation == execution.activation)
			continue;
		PsychicContribution contribution;
		contribution.grant = grant;
		contribution.ability = execution.definition;
		contribution.activation = execution.activation;
		contribution.aim_x = execution.aim_x;
		contribution.aim_y = execution.aim_y;
		const auto &profile = definition.power(contribution.ability);
		if (profile.kind == "scramble" && execution.target != ship)
			if (const auto pose = targets.projection_target_pose(execution.target))
				for (std::int64_t g = 0; g < profile.ghosts; ++g)
				{
					PsychicGhostShape shape;
					shape.pose.position.x = contribution.aim_x + profile.offset;
					shape.pose.position.y = contribution.aim_y + (g - (profile.ghosts - 1) / 2) * profile.spacing;
					shape.pose.heading = pose->heading;
					shape.pose.altitude = pose->altitude;
					contribution.shapes.push_back(shape);
				}
		contributions.insert_or_assign(grant, std::move(contribution));
	}
}
bool valid_psychic_shape(const PsychicGhostShape &shape)
{
	const auto in = [](std::int64_t value, std::int64_t lo, std::int64_t hi)
	{
		return value >= lo && value <= hi;
	};
	return in(shape.pose.position.x, -1000000000, 1000000000) && in(shape.pose.position.y, -1000000000, 1000000000) &&
	       in(shape.motion.velocity.x, -1000000, 1000000) && in(shape.motion.velocity.y, -1000000, 1000000) &&
	       in(shape.pose.altitude, -1000000, 1000000) && in(shape.pose.heading, 0, 359999) &&
	       in(shape.motion.remainder.x, 1 - simulation_tick_hz, simulation_tick_hz - 1) &&
	       in(shape.motion.remainder.y, 1 - simulation_tick_hz, simulation_tick_hz - 1) &&
	       in(shape.acceleration_remainder, 0, simulation_tick_hz - 1) &&
	       in(shape.turn_remainder, 0, simulation_tick_hz - 1);
}

PsychicChoirState restore_psychic_contributions(const Json &saved, EntityId ship, const std::string &apparatus,
                                                const AbilityRuntime *runtime, const PsychicDefinition &definition)
{
	auto state = saved.get<PsychicChoirState>();
	if (Json(state) != saved || state.contributions.size() > 256)
		throw std::runtime_error("Invalid saved psychic contributions");
	for (const auto &[grant, contribution] : state.contributions)
	{
		const auto *execution = find_psychic_execution(runtime, contribution);
		if (grant != contribution.grant || !execution || execution->apparatus != apparatus ||
		    !definition.has_power(contribution.ability) || !contribution.activation ||
		    (execution->phase != "active" && !(execution->phase == "paused" && execution->resume_phase == "active")) ||
		    contribution.aim_x != execution->aim_x || contribution.aim_y != execution->aim_y)
			throw std::runtime_error("Invalid psychic execution link");
		const auto &profile = definition.power(contribution.ability);
		if ((profile.kind == "scramble" && (contribution.shapes.size() != static_cast<std::size_t>(profile.ghosts) ||
		                                    !execution->target || execution->target == ship)) ||
		    (profile.kind != "scramble" && !contribution.shapes.empty()))
			throw std::runtime_error("Contradictory psychic projection");
		for (const auto &shape : contribution.shapes)
			if (!valid_psychic_shape(shape))
				throw std::runtime_error("Invalid apparent trajectory");
	}
	if (runtime)
		for (const auto &[grant, execution] : runtime->grants)
			if (execution.apparatus == apparatus && definition.has_power(execution.definition) &&
			    (execution.phase == "active" || (execution.phase == "paused" && execution.resume_phase == "active")) &&
			    !state.contributions.contains(grant))
				throw std::runtime_error("Missing saved psychic contribution");
	return state;
}
} // namespace phoenix
