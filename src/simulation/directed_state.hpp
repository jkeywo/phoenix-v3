#pragma once
namespace phoenix
{
// Intent and execution belong to the capability. Native mechanisms retain physical
// commitments (loaded rounds, turret motion and acquisition evidence).
struct DirectedRuntime
{
	std::size_t operating_mode = 0, targeting_mode = 0;
	EntityId target = 0;
	bool ready = false;
	std::string blocker;
	std::uint64_t sequence = 0;
	std::string loading_mode, loaded_mode;
	std::int64_t beam_funding_remainder = 0;
};
} // namespace phoenix
