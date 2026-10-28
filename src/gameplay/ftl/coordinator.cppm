module;
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

export module phoenix.gameplay.ftl.coordinator;
export import phoenix.contracts;
export import phoenix.content.controllers.types;

export namespace phoenix
{
using DepartureGroups = std::map<EntityId, std::set<EntityId>>;
using DepartureRequest = std::tuple<ConsoleAddress, OperatingCommand, bool>;
struct DepartureGroupFacts
{
	std::function<bool(EntityId)> unavailable;
	std::function<bool(EntityId, EntityId)> same_faction;
};
// A capture contains addressed drive ownership and current Controller grants only;
// no private brain memory, ECS registry or complete compiled definition escapes.
struct DepartureDriveCapability
{
	std::string system;
	ConsoleAddress owner;
};
struct DepartureControllerGrant
{
	std::string console, controller;
	bool human = false;
	std::uint64_t generation = 0;
	std::vector<std::string> physical_consumers;
};
struct DepartureCapabilities
{
	std::vector<DepartureDriveCapability> drives;
	std::vector<DepartureControllerGrant> controllers;
};
// Reads are synchronous. A batch is fully admitted before the native caller
// dissipates reserves, freezes reports or retires any actor.
struct DepartureAdmissionFacts
{
	std::function<bool(EntityId ship)> departed;
	std::function<bool(EntityId ship)> unavailable;
	std::function<DepartureCapabilities(EntityId ship)> capabilities;
	std::function<bool(EntityId leader, EntityId member)> member_authorised;
	std::function<std::int64_t(EntityId ship)> visitors;
	std::function<std::string(EntityId ship, const std::string &system)> reserve_blocker;
};
using AcceptedDeparture = std::pair<ConsoleAddress, OperatingCommand>;
ConsoleAddress departure_console_owner(EntityId ship, const std::string &system, const ControllableLayout &layout,
                                       const std::vector<std::string> &fitted_system_ids);

// Owns persistent group goals and the transient admitted departure queue. Actual
// readiness, authority, reserve consumption and report capture remain native
// operations at the explicit World departure phase.
class DepartureCoordinator
{
public:
	DepartureCoordinator();
	~DepartureCoordinator();
	const DepartureGroups &groups() const;
	void assign(EntityId leader, const std::set<EntityId> &members);
	void prune(const DepartureGroupFacts &);
	EntityId leader(EntityId member, const std::function<bool(EntityId)> &available) const;
	void erase_group(EntityId leader, bool cancel_member_requests);
	void capture(EntityId target, EntityId new_owner);
	void request(const ConsoleAddress &, const OperatingCommand &, bool ai);
	std::vector<DepartureRequest> take_requests();
	Json save_groups() const;
	void restore_groups(const Json &);

	// Capabilities must come from validated native bindings at this call, not a
	// cached pre-capture authority snapshot. execute=false never queues a request.
	bool admit(const DepartureAdmissionFacts &facts, const ConsoleAddress &address, const OperatingCommand &command,
	           bool execute, bool ai = false);
	std::string group_blocker(const DepartureAdmissionFacts &facts, EntityId ship) const;
	std::vector<AcceptedDeparture> prepare_pending(const DepartureAdmissionFacts &facts);
	// Preserve restore order: validate members, discard identifiable obsolete
	// encounter goals, then validate authority. Neither validator mutates groups.
	void validate_group_members(const std::function<bool(EntityId ship)> &known_ship) const;
	void validate_group_authority(const std::function<bool(EntityId leader, EntityId member)> &authorised) const;
	void
	discard_obsolete_groups(const std::function<bool(EntityId leader, const std::set<EntityId> &members)> &obsolete);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
