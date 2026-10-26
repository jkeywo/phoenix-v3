#pragma once
namespace phoenix
{
struct AbilityGrantState
{
	std::string id, definition, source, owner, parent, grant_effect, control, apparatus, phase = "ready", resume_phase;
	EntityId target = 0;
	std::uint64_t generation = 0, activation = 0, request_id = 0;
	std::int64_t expiry = -1, progress = 0, aim_x = 0, aim_y = 0;
	// Derived from current intent, qualification and the frozen native grant.
	bool requested = false;
	bool funding_blocked = false;
	std::int64_t output_permille = 0;
	std::int64_t funding_permille = 0, fault_permille = 0;
	std::string blocker;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AbilityGrantState, id, definition, source, owner, parent, grant_effect, control,
                                   apparatus, phase, resume_phase, target, generation, activation, request_id, expiry,
                                   progress, aim_x, aim_y)
struct AbilityRuntime
{
	std::map<std::string, AbilityGrantState> grants;
	std::map<std::string, std::uint64_t> recovery;
	std::uint64_t next_generation = 1, next_activation = 1;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AbilityRuntime, grants, recovery, next_generation, next_activation)
} // namespace phoenix
