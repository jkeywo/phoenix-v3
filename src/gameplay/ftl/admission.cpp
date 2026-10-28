module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
module phoenix.gameplay.ftl.coordinator;
namespace phoenix
{
ConsoleAddress departure_console_owner(EntityId ship, const std::string &system, const ControllableLayout &layout,
                                       const std::vector<std::string> &fitted_system_ids)
{
	for (const auto &controller : layout.controllers)
		for (const auto &channel : controller.commands)
			if (channel.action == OperatingAction::SystemEnabled)
				for (const auto index : channel.consumers)
					if (fitted_system_ids.at(index) == system)
						return {ship, controller.console};
	return {};
}
std::string DepartureCoordinator::group_blocker(const DepartureAdmissionFacts &facts, EntityId ship) const
{
	const auto group_leader = leader(ship,
	                                 [&](EntityId candidate)
	                                 {
		                                 return !facts.unavailable(candidate);
	                                 });
	if (!groups().contains(group_leader))
		return {};
	for (const auto member : groups().at(group_leader))
		if (!facts.unavailable(member))
		{
			if (!facts.member_authorised(group_leader, member))
				return "Departure group ownership changed";
			if (member != group_leader && facts.visitors(member))
				return "Friendly visitors await recovery";
			const auto capabilities = facts.capabilities(member);
			if (capabilities.drives.empty() ||
			    !facts.reserve_blocker(member, capabilities.drives.front().system).empty())
				return "Departure group not ready";
		}
	return {};
}
bool DepartureCoordinator::admit(const DepartureAdmissionFacts &facts, const ConsoleAddress &address,
                                 const OperatingCommand &command, bool execute, bool ai)
{
	if (command.action != "ftl.depart" || facts.departed(address.ship) || command.value || !command.channel.empty() ||
	    command.request_id || !command.arguments.empty() || !command.text.empty() || !command.speaker_name.empty() ||
	    !command.speaker_avatar.empty())
		return false;
	const auto capabilities = facts.capabilities(address.ship);
	const auto drive = std::find_if(capabilities.drives.begin(), capabilities.drives.end(),
	                                [&](const DepartureDriveCapability &candidate)
	                                {
		                                return candidate.system == command.system;
	                                });
	if (drive == capabilities.drives.end() || drive->owner != address)
		return false;
	bool authority = false;
	for (const auto &controller : capabilities.controllers)
		if (controller.console == address.console && controller.controller == command.controller &&
		    controller.human != ai && controller.generation == command.generation)
			for (const auto &system : controller.physical_consumers)
				authority |= system == command.system;
	if (!authority)
		return false;
	if (!facts.reserve_blocker(address.ship, command.system).empty() || !group_blocker(facts, address.ship).empty() ||
	    leader(address.ship,
	           [&](EntityId candidate)
	           {
		           return !facts.unavailable(candidate);
	           }) != address.ship)
		return false;
	if (execute)
		request(address, command, ai);
	return true;
}
std::vector<AcceptedDeparture> DepartureCoordinator::prepare_pending(const DepartureAdmissionFacts &facts)
{
	auto pending = take_requests();
	std::sort(pending.begin(), pending.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::get<0>(a).ship < std::get<0>(b).ship;
	          });
	std::vector<AcceptedDeparture> accepted;
	std::set<EntityId> seen;
	for (const auto &[address, command, ai] : pending)
		if (!seen.contains(address.ship) && admit(facts, address, command, false, ai))
		{
			accepted.emplace_back(address, command);
			seen.insert(address.ship);
		}
	if (accepted.empty())
		return accepted;
	// Revalidation is complete before the caller performs any physical departure.
	const auto leaders = accepted;
	for (const auto &[address, command] : leaders)
		if (groups().contains(address.ship))
			for (auto member : groups().at(address.ship))
				if (member != address.ship && !facts.unavailable(member) && !seen.contains(member))
				{
					const auto capabilities = facts.capabilities(member);
					const auto &drive = capabilities.drives.at(0);
					OperatingCommand companion{member, "ftl.depart", 0, drive.system};
					accepted.emplace_back(drive.owner, companion);
					seen.insert(member);
				}
	return accepted;
}
void DepartureCoordinator::validate_group_members(const std::function<bool(EntityId ship)> &known_ship) const
{
	std::set<EntityId> grouped_members;
	for (const auto &[group_leader, members] : groups())
	{
		if (!known_ship(group_leader) || !members.contains(group_leader) || members.size() > 32)
			throw std::runtime_error("Invalid departure group");
		for (auto member : members)
			if (!known_ship(member) || !grouped_members.insert(member).second)
				throw std::runtime_error("Unknown or duplicate departure member");
	}
}
void DepartureCoordinator::validate_group_authority(
    const std::function<bool(EntityId leader, EntityId member)> &authorised) const
{
	for (const auto &[group_leader, members] : groups())
		for (auto member : members)
			if (!authorised(group_leader, member))
				throw std::runtime_error("Unknown or duplicate departure member");
}
void DepartureCoordinator::discard_obsolete_groups(
    const std::function<bool(EntityId leader, const std::set<EntityId> &members)> &obsolete)
{
	std::vector<EntityId> obsolete_groups;
	for (const auto &[group_leader, members] : groups())
		if (obsolete(group_leader, members))
			obsolete_groups.push_back(group_leader);
	for (const auto group_leader : obsolete_groups)
		erase_group(group_leader, true);
}
} // namespace phoenix
