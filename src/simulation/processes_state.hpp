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
	std::string phase = "off", blocker;
	std::vector<RuleWorkCredit> idle_credits, work_credits;
	std::int64_t work_remainder = 0;
	// Derived quote/activity values are frozen by the existing work phase.
	std::int64_t idle_output = 0, work_output = 0, actual_work = 0, rate_permille = 1000;
	bool operator==(const ProcessState &) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProcessState, id, slot, definition, source, grant_effect, native_id, generation, run,
                                   expiry, started, last_work, requested, admitted, origin, phase, idle_credits,
                                   work_credits, work_remainder, idle_output, work_output, actual_work, rate_permille,
                                   blocker)
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
