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
 std::map<std::string,std::uint64_t> generations;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DockPortState, accepting, supply, permitted)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DockingState, ports, revision)

}
