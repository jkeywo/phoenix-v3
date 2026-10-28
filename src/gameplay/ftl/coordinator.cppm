module;
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <tuple>
#include <vector>

export module phoenix.gameplay.ftl.coordinator;
export import phoenix.contracts;

export namespace phoenix
{
using DepartureGroups = std::map<EntityId, std::set<EntityId>>;
using DepartureRequest = std::tuple<ConsoleAddress, OperatingCommand, bool>;
struct DepartureGroupFacts
{
	std::function<bool(EntityId)> unavailable;
	std::function<bool(EntityId, EntityId)> same_faction;
};
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

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
