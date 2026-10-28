module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.gameplay.ftl.coordinator;

namespace phoenix
{
struct DepartureCoordinator::Impl
{
	DepartureGroups groups;
	std::vector<DepartureRequest> pending;
	void cancel_requests(const std::set<EntityId> &members)
	{
		std::erase_if(pending,
		              [&](const auto &request)
		              {
			              return members.contains(std::get<0>(request).ship);
		              });
	}
};
DepartureCoordinator::DepartureCoordinator() : impl_(std::make_unique<Impl>())
{
}
DepartureCoordinator::~DepartureCoordinator() = default;
const DepartureGroups &DepartureCoordinator::groups() const
{
	return impl_->groups;
}
void DepartureCoordinator::assign(EntityId leader, const std::set<EntityId> &members)
{
	for (auto it = impl_->groups.begin(); it != impl_->groups.end();)
		if (std::any_of(members.begin(), members.end(),
		                [&](EntityId id)
		                {
			                return it->second.contains(id);
		                }))
			it = impl_->groups.erase(it);
		else
			++it;
	impl_->groups[leader] = members;
}
void DepartureCoordinator::prune(const DepartureGroupFacts &facts)
{
	DepartureGroups next;
	for (const auto &[original, members] : impl_->groups)
	{
		auto leader = original;
		auto group = members;
		std::erase_if(group,
		              [&](EntityId id)
		              {
			              return facts.unavailable(id) || !facts.same_faction(id, leader);
		              });
		if (group.empty())
			continue;
		if (!group.contains(leader))
			leader = *group.begin();
		next[leader] = std::move(group);
	}
	impl_->groups = std::move(next);
}
EntityId DepartureCoordinator::leader(EntityId member, const std::function<bool(EntityId)> &available) const
{
	for (const auto &[leader, group] : impl_->groups)
		if (group.contains(member) && available(leader))
			return leader;
	return member;
}
void DepartureCoordinator::erase_group(EntityId leader, bool cancel_member_requests)
{
	const auto found = impl_->groups.find(leader);
	if (found == impl_->groups.end())
		return;
	if (cancel_member_requests)
		impl_->cancel_requests(found->second);
	impl_->groups.erase(found);
}
void DepartureCoordinator::capture(EntityId target, EntityId new_owner)
{
	std::set<EntityId> obsolete_members;
	for (auto it = impl_->groups.begin(); it != impl_->groups.end();)
	{
		if (it->first == target || it->second.contains(target))
		{
			obsolete_members.insert(it->first);
			obsolete_members.insert(it->second.begin(), it->second.end());
			it = impl_->groups.erase(it);
		}
		else
			++it;
	}
	impl_->cancel_requests(obsolete_members);
	impl_->groups[new_owner].insert(new_owner);
	impl_->groups[new_owner].insert(target);
}
void DepartureCoordinator::request(const ConsoleAddress &address, const OperatingCommand &command, bool ai)
{
	impl_->pending.emplace_back(address, command, ai);
}
std::vector<DepartureRequest> DepartureCoordinator::take_requests()
{
	auto result = std::move(impl_->pending);
	impl_->pending.clear();
	return result;
}
Json DepartureCoordinator::save_groups() const
{
	return impl_->groups;
}
void DepartureCoordinator::restore_groups(const Json &saved)
{
	impl_->groups = saved.get<DepartureGroups>();
}
} // namespace phoenix
