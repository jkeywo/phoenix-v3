module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

module phoenix.effects.process_validation;
import phoenix.data;

namespace phoenix
{
void validate_process_runtime(const CustomPropertyAddress &address, const ProcessRuntime &runtime,
                              ProcessValidationDefinitions definitions, const ProcessValidationPorts &ports,
                              const RuleContinuationValidator &continuation, std::int64_t tick)
{
	constexpr auto maximum = 9007199254740991ULL;
	if (!runtime.next_generation || runtime.next_generation > process_generation_limit || !runtime.next_run ||
	    runtime.next_run > maximum || runtime.processes.size() > process_owner_limit ||
	    runtime.work.size() > process_owner_limit || runtime.work_tick < -1 || runtime.work_tick > tick)
		throw std::runtime_error("Invalid process runtime bounds");
	const auto initial = ports.initial();
	std::set<std::uint64_t> generations, runs;
	for (const auto &[id, state] : runtime.processes)
	{
		if (id != state.id || id.empty() || id.size() > 1024 || !valid_identifier(state.slot) ||
		    !definitions.processes.contains(state.definition) || !state.generation ||
		    state.generation >= runtime.next_generation || !generations.insert(state.generation).second ||
		    state.run >= runtime.next_run || state.started < -1 || state.started > tick || state.last_work < -1 ||
		    state.last_work > tick || state.blocker.size() > 1024 || state.idle_output < 0 ||
		    state.idle_output > 1000 || state.work_output < 0 || state.work_output > 1000 || state.actual_work < 0 ||
		    state.actual_work > resource_limit || state.rate_permille < 0 || state.rate_permille > 1000000 ||
		    state.work_remainder < 0 || state.work_remainder >= definitions.processes.at(state.definition).work_divisor)
			throw std::runtime_error("Invalid process execution state");
		if (state.phase == "running" ? state.idle_output <= 0 : state.idle_output || state.work_output)
			throw std::runtime_error("Contradictory process activity");
		if (state.phase != "off" && state.phase != "running" && state.phase != "paused" && state.phase != "completed")
			throw std::runtime_error("Invalid process phase");
		if (state.admitted ? (!state.requested || !state.run || state.phase == "off" || state.started < 0 ||
		                      !runs.insert(state.run).second)
		                   : (state.phase == "running" ||
		                      (state.completed ? state.phase != "completed"
		                                       : (state.requested ? state.phase != "paused" : state.phase != "off"))))
			throw std::runtime_error("Contradictory process lifecycle");
		if (state.target.size() > 1024 || state.progress < 0 || state.progress > resource_limit ||
		    state.required_work < 0 || state.required_work > resource_limit ||
		    (state.phase == "completed" && !state.completed) ||
		    (definitions.processes.at(state.definition).required_work &&
		     (state.required_work != definitions.processes.at(state.definition).required_work ||
		      state.progress > state.required_work)))
			throw std::runtime_error("Invalid finite process continuation");
		if (!state.admitted && (!state.idle_credits.empty() || !state.work_credits.empty() || state.work_remainder))
			throw std::runtime_error("Unadmitted process retained paid work");
		continuation.context(Json(state.origin));
		if (!state.parent_process.empty())
		{
			const auto parent = runtime.processes.find(state.parent_process);
			if (parent == runtime.processes.end() || !parent->second.parent_process.empty() ||
			    parent->second.generation >= state.generation || parent->second.definition != state.definition ||
			    parent->second.native_id != state.native_id || parent->second.source != state.source ||
			    parent->second.grant_effect != state.grant_effect || parent->second.slot != state.slot ||
			    parent->second.expiry != state.expiry || state.target.empty() ||
			    (!parent->second.requested && state.requested) || !id.starts_with("episode:"))
				throw std::runtime_error("Invalid native target episode lineage");
			const auto target_hash = sha256(std::span<const std::uint8_t>(
			    reinterpret_cast<const std::uint8_t *>(state.target.data()), state.target.size()));
			if (id != "episode:" + std::to_string(parent->second.generation) + ":" + target_hash)
				throw std::runtime_error("Native target episode identity changed");
			const auto native = definitions.processes.at(state.definition).native;
			if (native != NativeProcessKind::Repair && native != NativeProcessKind::Medical &&
			    native != NativeProcessKind::Recovery && native != NativeProcessKind::ServicingEnergy &&
			    native != NativeProcessKind::ServicingAmmunition)
				throw std::runtime_error("Non-native process has target episodes");
		}
		else if (state.source.empty())
		{
			const auto expected = initial.processes.find(id);
			if (expected == initial.processes.end() || state.definition != expected->second.definition ||
			    state.slot != expected->second.slot || state.native_id != expected->second.native_id ||
			    !state.grant_effect.empty() || state.expiry != -1)
				throw std::runtime_error("Saved static process declaration changed");
		}
		else
		{
			const auto grant = definitions.grants.find(state.grant_effect);
			if (grant == definitions.grants.end() || grant->second.process != state.definition ||
			    ports.native_binding(grant->second) != state.native_id || id != "temporary:" + state.source ||
			    state.source.size() > 512 ||
			    (grant->second.duration_ticks == 0
			         ? state.expiry != -1
			         : state.expiry < 0 || state.expiry > tick + grant->second.duration_ticks))
				throw std::runtime_error("Invalid temporary process lineage/lifetime");
		}
		for (const auto *credits : {&state.idle_credits, &state.work_credits})
		{
			if (credits->size() > 4096)
				throw std::runtime_error("Process work credit capacity exhausted");
			std::set<
			    std::tuple<EntityId, std::string, std::string, std::string, std::string, std::string, std::int64_t>>
			    distinct;
			for (const auto &credit : *credits)
			{
				continuation.credit(Json(credit));
				const auto denomination = credits == &state.idle_credits
				                              ? resource_work_unit * 1000
				                              : definitions.processes.at(state.definition).work_divisor * 1000000;
				if (credit.unit_work != denomination)
					throw std::runtime_error("Saved process credit has wrong work denomination");
				if (!distinct
				         .emplace(credit.payer, credit.store, credit.resource, credit.property, credit.equipment,
				                  credit.officer, credit.compartment)
				         .second)
					throw std::runtime_error("Duplicate process work credit");
			}
		}
	}
	for (const auto &[id, state] : initial.processes)
		if (!runtime.processes.contains(id))
			throw std::runtime_error("Missing static process declaration");
	std::set<std::pair<std::uint64_t, std::uint64_t>> receipts;
	for (const auto &receipt : runtime.work)
	{
		const auto found = definitions.processes.find(receipt.definition);
		if (found == definitions.processes.end() || found->second.family != receipt.family ||
		    found->second.work_unit != receipt.work_unit || receipt.source != process_owner_kind(address) ||
		    receipt.owner != process_owner_name(address) || receipt.execution.empty() ||
		    receipt.execution.size() > 1024 || !receipt.activation || receipt.activation >= runtime.next_run ||
		    !receipt.generation || receipt.generation >= runtime.next_generation || receipt.work <= 0 ||
		    receipt.work > resource_limit || receipt.output <= 0 || receipt.output > 1000 ||
		    !receipts.emplace(receipt.generation, receipt.activation).second)
			throw std::runtime_error("Invalid completed process work receipt");
	}
}
} // namespace phoenix
