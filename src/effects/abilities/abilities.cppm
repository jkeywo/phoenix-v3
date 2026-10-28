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

export module phoenix.effects.abilities;
export import phoenix.effects.contracts;

export namespace phoenix
{
struct AbilityGrantState
{
	std::string id, definition, source, owner, parent, grant_effect, control, apparatus, phase = "ready", resume_phase;
	EntityId target = 0;
	std::uint64_t generation = 0, activation = 0, request_id = 0;
	std::int64_t expiry = -1, progress = 0, aim_x = 0, aim_y = 0;
	// Derived from current intent, qualification and the frozen native grant.
	bool requested = false;
	bool retained_request = false;
	bool funding_blocked = false;
	std::int64_t output_permille = 0;
	std::int64_t funding_permille = 0, fault_permille = 0;
	std::string blocker;
	std::vector<RuleWorkCredit> credits;
	bool operator==(const AbilityGrantState &) const = default;
};
void to_json(Json &, const AbilityGrantState &);
void from_json(const Json &, AbilityGrantState &);
using AbilityWorkReceipt = ExecutionWorkReceipt;
struct AbilityRuntime
{
	std::map<std::string, AbilityGrantState> grants;
	std::map<std::string, std::uint64_t> recovery;
	std::uint64_t next_generation = 1, next_activation = 1;
	std::uint64_t work_tick = 0;
	std::vector<AbilityWorkReceipt> work;
	bool operator==(const AbilityRuntime &) const = default;
};
void to_json(Json &, const AbilityRuntime &);
void from_json(const Json &, AbilityRuntime &);

struct AbilityGrantSeed
{
	std::string source, owner, definition;
};
AbilityRuntime initial_ability_runtime(std::span<const AbilityGrantSeed>);
bool ability_running(const AbilityGrantState &);
std::string ability_recovery_key(const AbilityGrantState &);
std::string ability_exclusion_key(const AbilityGrantState &, const AbilityExclusionDefinition &);
std::uint64_t ability_recovery_until(const AbilityRuntime &, const AbilityGrantState &, const AbilityDefinition &);
void ability_begin_recovery(AbilityRuntime &, const AbilityGrantState &, const AbilityDefinition &, std::uint64_t tick);
std::vector<std::string> ability_recovery_reservations(const AbilityGrantState &, const AbilityDefinition &);
bool ability_recovery_capacity(const AbilityRuntime &, const AbilityGrantState &, const AbilityDefinition &,
                               std::uint64_t tick);
void install_ability_grant(AbilityRuntime &, const std::string &source, const std::string &owner,
                           const std::string &parent, const std::string &effect, const AbilityGrantDefinition &,
                           std::int64_t tick);

} // namespace phoenix
