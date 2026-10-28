module;
#include <flecs.h>
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

export module phoenix.effects.rule_state;
export import phoenix.effects.contracts;

export namespace phoenix
{
struct RuleExecutionState
{
	std::string key, definition, binding, source_key;
	RuleOwnerKind owner_kind = RuleOwnerKind::Equipment;
	NativeRuleContext context, applied_context;
	std::uint64_t generation = 0;
	std::int64_t started = 0, next = 0, last = -1;
	// Paid work leases the next native phase, so prices/rates cannot change
	// between resource quotation and physical actuation.
	std::int64_t lease_tick = -1, lease_permille = 0;
	std::int64_t stop_tick = -1;
	bool eligible = false, active = false, stopped = false;
	std::vector<RuleWorkCredit> credits;
	// Last applied native-phase output, retained across a phase-boundary save.
	std::int64_t output = 0;
	std::string blocker;
	bool operator==(const RuleExecutionState &) const = default;
};
struct ActiveRuleEffect
{
	std::uint64_t id = 0;
	std::string source, definition, group_source;
	PayloadHitContext context;
	NativeRuleContext origin;
	std::int64_t expiry = 0;
	std::map<std::string, RuleExecutionState> clocks;
	bool operator==(const ActiveRuleEffect &) const = default;
};
struct ActiveRuleTarget
{
	flecs::entity body;
};
struct RuleRuntime
{
	std::uint64_t next_generation = 1;
	std::map<std::string, RuleExecutionState> executions;
	std::set<std::string> regions;
	bool membership_initialised = false;
};

enum class RuleEventPhase
{
	Payload,
	Shield,
	Region,
	Source,
	Periodic
};
struct PendingRuleExecution
{
	std::uint64_t id = 0;
	std::int64_t due = 0;
	RuleEventPhase phase = RuleEventPhase::Payload;
	std::string rule, source_key;
	std::int64_t damage = 0;
	NativeRuleContext context;
};

void to_json(Json &, const RuleExecutionState &);
void from_json(const Json &, RuleExecutionState &);
void to_json(Json &, const RuleRuntime &);
void from_json(const Json &, RuleRuntime &);
void to_json(Json &, const ActiveRuleEffect &);
void from_json(const Json &, ActiveRuleEffect &);
void to_json(Json &, const PendingRuleExecution &);
void from_json(const Json &, PendingRuleExecution &);

} // namespace phoenix
