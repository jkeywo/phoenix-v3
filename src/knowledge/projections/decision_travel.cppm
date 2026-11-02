module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

export module phoenix.knowledge.decision_travel;
export import phoenix.contracts;

export namespace phoenix
{
struct DockingGuidance
{
	std::int64_t x = 0, y = 0;
	bool navigate = false;
};
struct DockingPortDecision
{
	std::string id, controller, compatibility, phase = "unknown";
	std::int64_t x_mm = 0, y_mm = 0, heading_mdeg = 0, progress = 0;
	bool accepting = false, supply = false, working = false, emergency = false;
	bool follower = false, assisted = false;
	EntityId target = 0;
};
struct DockingTargetDecision
{
	EntityId entity = 0;
	std::string port, compatibility;
	std::int64_t x_mm = 0, y_mm = 0, heading_mdeg = 0;
	bool available = false;
	std::vector<std::string> local_ports;
};
struct DockingDecisionSnapshot
{
	std::uint64_t revision = 1;
	bool observed = false, fitted = false, complete = true;
	std::vector<DockingPortDecision> ports;
	std::vector<DockingTargetDecision> targets;
	std::optional<DockingGuidance> guidance;
};
struct LogisticsStoreDecision
{
	std::string id, system, controller, selected_resource;
	std::size_t index = 0;
	bool main = false, held = false, working = false, single_resource = false;
	std::map<std::string, std::int64_t> available, quantities, space, targets;
	std::vector<std::string> resource_choices, selectable_resources;
	std::map<std::string, std::string> resource_labels;
};
struct LogisticsOfferDecision
{
	EntityId entity = 0;
	std::string store, material;
	std::size_t index = 0;
	bool connected = false;
};
struct LogisticsRouteDecision
{
	std::string from, to;
	bool closed = false;
};
struct LogisticsServiceDecision
{
	std::string bay, native, process;
	std::uint64_t generation = 0;
};
struct LogisticsTransitDecision
{
	std::uint64_t id = 0, request = 0, cargo = 0;
	EntityId source = 0, destination = 0, location = 0;
	std::size_t from = 0, to = 0, compartment = 0;
	std::string material;
	std::int64_t remaining = 0, quantity = 0, edge_to = -1, travel = 0, crossing = 0;
	bool automatic = false, returning = false;
	LogisticsServiceDecision service;
};
struct LogisticsCargoDecision
{
	std::uint64_t id = 0, report_tick = 0;
	EntityId carrier = 0, target = 0;
	std::string material, phase;
	std::int64_t quantity = 0;
};
struct LogisticsCraftStoreDecision
{
	std::string id;
	std::vector<std::string> accepts;
};
struct LogisticsCraftDecision
{
	EntityId id = 0;
	bool authorised = false, controllable = false, available = false;
	std::vector<LogisticsCraftStoreDecision> stores;
};
struct LogisticsDecisionSnapshot
{
	std::uint64_t revision = 1;
	bool observed = false, manual = false;
	std::string controller, route_controller, cargo_authorisation_controller;
	std::vector<LogisticsStoreDecision> stores;
	std::vector<LogisticsOfferDecision> offers;
	std::vector<LogisticsRouteDecision> routes;
	std::vector<LogisticsTransitDecision> requests, batches;
	std::vector<LogisticsCargoDecision> cargo;
	std::vector<LogisticsCraftDecision> cargo_craft;
};
struct FtlDecisionRow
{
	std::string system, blocker;
	std::int64_t energy = 0, capacity = 0, power_draw = 0, heat = 0, visitors = 0;
	bool enabled = false, ready = false, work_completed = false;
	std::vector<std::string> left_behind;
};
Json docking_decision_json(const DockingDecisionSnapshot &);
Json logistics_decision_json(const LogisticsDecisionSnapshot &);
Json ftl_decision_json(const std::vector<FtlDecisionRow> &);
// Explicit provider/debug input decoding; never used by ordinary tick capture.
DockingDecisionSnapshot docking_decision_from_json(const Json &);
LogisticsDecisionSnapshot logistics_decision_from_json(const Json &);
std::vector<FtlDecisionRow> ftl_decision_from_json(const Json &);
} // namespace phoenix
