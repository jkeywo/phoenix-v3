#pragma once
namespace phoenix
{
struct DockPortState
{
	bool accepting = false, supply = false;
	std::set<EntityId> permitted;
};
struct DockingState
{
	std::vector<DockPortState> ports;
	std::uint64_t revision = 1;
};
struct DockConnection
{
	EntityId parent = 0;
	std::size_t port = 0, remote_port = 0;
	std::string phase = "approach", controller;
	std::int64_t progress = 0, relative_heading = 0, relative_z = 0;
	fixed::Vec relative;
	bool assisted = false;
	std::uint64_t generation = 0;
	std::map<std::string, std::uint64_t> generations;
	// Inertial ships without lateral thrust brake, align, then translate on a latched course.
	int guidance_step = 0;
	std::int64_t guidance_heading = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DockPortState, accepting, supply, permitted)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DockingState, ports, revision)

} // namespace phoenix
