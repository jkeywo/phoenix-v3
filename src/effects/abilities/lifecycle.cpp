module;
#include <flecs.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.effects.ability_lifecycle;
import phoenix.control.controllers;

namespace phoenix
{
AbilityLifecycleNative::~AbilityLifecycleNative() = default;
AbilityLifecycle::AbilityLifecycle(flecs::entity actor, const AbilityCatalog &catalogue, std::uint64_t tick,
                                   AbilityLifecycleNative &native)
    : actor_(actor), catalogue_(catalogue), tick_(tick), native_(native)
{
}
const AbilityGrantState *AbilityLifecycle::find(const std::string &key) const
{
	const auto *runtime = actor_.try_get<AbilityRuntime>();
	if (!runtime)
		return nullptr;
	const auto found = runtime->grants.find(key);
	return found == runtime->grants.end() ? nullptr : &found->second;
}
void stop_ability_run(AbilityRuntime &runtime, AbilityGrantState &g, const AbilityDefinition &d, std::uint64_t tick)
{
	if (!ability_running(g))
		return;
	const bool preparing = g.phase == "preparing" || g.resume_phase == "preparing";
	if (preparing && d.interruption_recovery_ticks)
	{
		const auto key = ability_recovery_key(g);
		runtime.recovery[key] = std::max(runtime.recovery[key], tick + d.interruption_recovery_ticks);
	}
	else if (!preparing && d.recovery_on == "stop")
		ability_begin_recovery(runtime, g, d, tick);
	g.phase = "ready";
	g.resume_phase.clear();
	g.progress = 0;
	g.output_permille = 0;
}

void AbilityLifecycle::activate(const std::string &key, bool activation_committed)
{
	const auto *before = find(key);
	if (!before)
		return;
	const auto snapshot = *before;
	const auto &d = catalogue_.definitions.at(snapshot.definition);
	const auto *control = native_.resolve_control(snapshot);
	const auto blocker =
	    control ? native_.eligibility_blocker(*control, snapshot, snapshot.target) : "No compatible ability control";
	if (!control || !blocker.empty() || !native_.apparatus_blocker(snapshot).empty())
	{
		auto &state = actor_.get_mut<AbilityRuntime>();
		auto &interrupted = state.grants.at(key);
		if (control && d.sustained && blocker == "Ability requirements unavailable" &&
		    actor_.get<EntityControls>()
		            .controllers.at(control->controller)
		            .definition.commands.at(control->enabled)
		            .semantics == CommandSemantics::Request)
			interrupted.retained_request = true;
		stop_ability_run(state, interrupted, d, tick_);
		native_.request_result(snapshot, false);
		return;
	}
	if (actor_.get<AbilityRuntime>().next_activation >= 9007199254740991ULL)
		throw std::runtime_error("Ability activation identity exhausted");
	const auto failure =
	    activation_committed ? std::string{} : native_.commit_activation(snapshot, d.activation_effect);
	auto &runtime = actor_.get_mut<AbilityRuntime>();
	const auto found = runtime.grants.find(key);
	if (found == runtime.grants.end() || found->second.generation != snapshot.generation)
		return;
	auto &g = found->second;
	if (!failure.empty())
	{
		if (d.preparation_ticks)
			stop_ability_run(runtime, g, d, tick_);
		else
			g.phase = "ready";
		g.blocker = failure;
		native_.request_result(snapshot, false);
		return;
	}
	g.retained_request = false;
	g.activation = runtime.next_activation++;
	g.phase = d.sustained ? "active" : "ready";
	g.progress = d.preparation_ticks * 1000;
	if (d.recovery_on == "activation" || !d.sustained)
		ability_begin_recovery(runtime, g, d, tick_);
}

void AbilityLifecycle::refresh_work(bool advance)
{
	auto *runtime = actor_.try_get_mut<AbilityRuntime>();
	if (!runtime)
		return;
	if (advance)
	{
		runtime->work.clear();
		runtime->work_tick = tick_;
	}
	std::vector<std::string> completed;
	for (auto &[key, g] : runtime->grants)
	{
		g.output_permille = 0;
		if (advance)
			g.funding_permille = 0;
		g.fault_permille = 0;
		if (!ability_running(g) || !native_.apparatus_blocker(g).empty())
			continue;
		const auto &d = catalogue_.definitions.at(g.definition);
		const auto preparing = g.phase == "preparing" || g.resume_phase == "preparing";
		const auto funding = advance ? native_.paid_output(key) : g.funding_permille;
		std::int64_t faults = 1000;
		if (!g.apparatus.empty())
			faults = native_.apparatus_effectiveness(g.apparatus);
		if ((g.funding_blocked || funding < 1000) && d.shortage != "proportional")
		{
			if (advance && d.shortage == "stop")
				stop_ability_run(*runtime, g, d, tick_);
			else if (advance && g.phase != "paused")
			{
				g.resume_phase = g.phase;
				g.phase = "paused";
			}
			g.blocker = "Insufficient ability funding";
			continue;
		}
		if (!advance && g.phase == "paused")
			continue;
		if (advance && g.phase == "paused")
		{
			g.phase = g.resume_phase;
			g.resume_phase.clear();
		}
		if (preparing)
		{
			if (advance)
			{
				g.progress = std::min(d.preparation_ticks * 1000, g.progress + funding);
				if (g.progress == d.preparation_ticks * 1000)
					completed.push_back(key);
			}
		}
		else
		{
			g.funding_permille = funding;
			g.fault_permille = faults;
			g.output_permille = funding * faults / 1000;
			if (advance && g.output_permille)
				runtime->work.push_back({g.source, g.owner, d.family, g.definition, g.activation, g.output_permille,
				                         g.id, g.output_permille, "permille", g.generation});
		}
	}
	for (const auto &key : completed)
	{
		auto *g = find(key);
		if (!g)
			continue;
		activate(key);
	}
}

} // namespace phoenix
