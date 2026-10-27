#pragma once
namespace phoenix
{
inline constexpr std::size_t process_owner_limit = 256;
inline constexpr std::uint64_t process_generation_limit = 1000000000ULL;
void to_json(Json &, const NativeRuleContext &);
void from_json(const Json &, NativeRuleContext &);
// The native owner stores execution intent and paid fractional work. Physical
// progress (generator ramps, impulse charge, recipe batches) stays native.
struct ProcessState
{
	std::string id, slot, definition, source, grant_effect, native_id;
	std::uint64_t generation = 0, run = 0;
	std::int64_t expiry = -1, started = -1, last_work = -1;
	bool requested = true, admitted = false;
	NativeRuleContext origin;
	std::string phase = "off", blocker, target, parent_process;
	std::int64_t progress = 0, required_work = 0;
	bool completed = false, ready = false;
	std::vector<RuleWorkCredit> idle_credits, work_credits;
	std::int64_t work_remainder = 0;
	// Derived quote/activity values are frozen by the existing work phase.
	std::int64_t idle_output = 0, work_output = 0, actual_work = 0, rate_permille = 1000;
	bool operator==(const ProcessState &) const = default;
};
namespace process_state_fields
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProcessState, id, slot, definition, source, grant_effect, native_id, generation, run,
                                   expiry, started, last_work, requested, admitted, origin, phase, idle_credits,
                                   work_credits, work_remainder, idle_output, work_output, actual_work, rate_permille,
                                   blocker, target, parent_process, progress, required_work, completed, ready)
} // namespace process_state_fields
// Schema 31 retains the public completion alias, but completion has one mutable
// owner. Failed consequences are attempted once too; there is no pending state.
inline void to_json(Json &value, const ProcessState &state)
{
	process_state_fields::to_json(value, state);
	value["completion_delivered"] = state.completed;
}
inline void from_json(const Json &value, ProcessState &state)
{
	process_state_fields::from_json(value, state);
	const auto &alias = value.at("completion_delivered");
	if (!alias.is_boolean() || alias.get<bool>() != state.completed)
		throw std::runtime_error("Invalid process completion alias");
}
using ProcessWorkReceipt = ExecutionWorkReceipt;
struct ProcessRuntime
{
	std::map<std::string, ProcessState> processes;
	std::uint64_t next_generation = 1, next_run = 1;
	std::int64_t work_tick = -1;
	std::vector<ProcessWorkReceipt> work;
	bool operator==(const ProcessRuntime &) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProcessRuntime, processes, next_generation, next_run, work_tick, work)
} // namespace phoenix
