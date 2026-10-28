module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

export module phoenix.effects.processes;
export import phoenix.effects.contracts;

export namespace phoenix
{
inline constexpr std::size_t process_owner_limit = 256;
inline constexpr std::uint64_t process_generation_limit = 1000000000ULL;
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

// Schema 31 retains the public completion alias, but completion has one mutable
// owner. Failed consequences are attempted once too; there is no pending state.
void to_json(Json &value, const ProcessState &state);
void from_json(const Json &value, ProcessState &state);
using ProcessWorkReceipt = ExecutionWorkReceipt;
struct ProcessRuntime
{
	std::map<std::string, ProcessState> processes;
	std::uint64_t next_generation = 1, next_run = 1;
	std::int64_t work_tick = -1;
	std::vector<ProcessWorkReceipt> work;
	bool operator==(const ProcessRuntime &) const = default;
};
void to_json(Json &, const ProcessRuntime &);
void from_json(const Json &, ProcessRuntime &);

struct RuleWorkInput
{
	std::int64_t rate = 0, credit = 0, purchased = 0, available = 0;
	bool electrical = false, blocked = false;
	// Credits and purchased work use this many subunits per physical input unit.
	// Ordinary continuous rates retain the native energy/time denominator.
	std::int64_t unit_work = resource_work_unit;
};
struct RuleWorkPayment
{
	std::int64_t output = 1000;
	std::vector<std::int64_t> debits, credits;
	bool shortage = false;
};

RuleWorkPayment quote_rule_work(std::span<const RuleWorkInput>, bool proportional, std::int64_t maximum_output = 1000);
RuleActor process_actor(const CustomPropertyAddress &);
std::string process_owner_kind(const CustomPropertyAddress &);
std::string process_owner_name(const CustomPropertyAddress &);
std::string process_instance_key(const CustomPropertyAddress &, const ProcessState &);
std::string process_run_source(const CustomPropertyAddress &, const ProcessState &);
ProcessRuntime initial_process_runtime(std::span<const ProcessBinding>, const ProcessCatalog &);
std::string install_process_grant(ProcessRuntime &, const ProcessCatalog &, const ProcessGrantDefinition &,
                                  const std::string &effect, const std::string &source, const std::string &slot,
                                  std::int64_t tick, const NativeRuleContext &origin);
void remove_process_grant(ProcessRuntime &, const std::string &source);
void reset_process_run_state(ProcessState &, const ProcessDefinition &, bool clear_request);
std::string stage_process_funding_state(ProcessState &, const ProcessDefinition &, std::int64_t tick, std::int64_t idle,
                                        std::int64_t activity);
std::optional<EffectGroupDefinition> process_funding_effect(const ProcessDefinition &, const ProcessState &,
                                                            std::int64_t tick, std::int64_t idle,
                                                            std::int64_t activity);
std::int64_t process_effect_work(const ProcessDefinition &, const ProcessState &, std::int64_t work);
std::int64_t process_next_work_remainder(const ProcessDefinition &, const ProcessState &, std::int64_t work);
void record_process_completion(ProcessState &, const ProcessDefinition &, const std::string &target);
bool record_process_work(ProcessRuntime &, const CustomPropertyAddress &, const ProcessDefinition &,
                         const ProcessState &frozen, std::int64_t tick, std::int64_t work, std::int64_t activity);

Json process_row_decision_facts(const Json &row);
bool process_has_finite_work(const ProcessDefinition &);
bool process_is_native_driver(const ProcessDefinition &, const ProcessState &);
Json process_decision_facts(const Observation &permitted);
bool process_job_details_visible(const ConsoleAddress &, const CustomPropertyAddress &, const ProcessState &,
                                 const ProcessDefinition &, const Observation *permitted);
} // namespace phoenix
