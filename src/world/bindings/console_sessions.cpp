module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix
{
using namespace world_detail;
std::vector<ConsoleSpec> World::consoles(EntityId ship) const
{
	return impl_->entities.contains(ship) ? impl_->definition(ship).consoles : std::vector<ConsoleSpec>{};
}

bool World::can_claim(std::uint64_t console, EntityId ship, const std::string &name, bool sidebar) const
{
	if (impl_->entities.contains(ship) && impl_->entities.at(ship).has<CraftState>())
		return false;
	if (!console || impl_->ownership.claims().contains(console) || !impl_->entities.contains(ship))
		return false;
	auto seat = impl_->seat(ship, name);
	if (!seat || !impl_->ownership.can_claim(console, {ship, seat->id}, sidebar))
		return false;
	const auto &controls = impl_->entities.at(ship).get<EntityControls>();
	return impl_->definition(ship).explicit_stations || !seat->definition.empty() ||
	       std::any_of(controls.controllers.begin(), controls.controllers.end(),
	                   [&](const auto &controller)
	                   {
		                   return controller.definition.console == seat->id;
	                   });
}

std::vector<EntityId> World::operational_fleet(EntityId origin) const
{
	std::vector<EntityId> out;
	if (!impl_->entities.contains(origin) || impl_->operational_owner(origin) != origin)
		return out;
	for (const auto &[id, ship] : impl_->entities)
		if ((id == origin || impl_->captured_by.contains(id)) && impl_->operational_owner(id) == origin &&
		    !ship.has<CraftState>())
			out.push_back(id);
	return out;
}

bool World::prepare_console_profile(const ConsoleAddress &address, const std::string &profile)
{
	impl_->clear_presentation_snapshots();
	if (ticks() || !impl_->ownership.claims().empty() || !impl_->entities.contains(address.ship))
		return false;
	auto entity = impl_->entities.at(address.ship);
	if (!entity.has<EntityControls>())
		return false;
	auto &controls = entity.get_mut<EntityControls>();
	if (!controls.profiles.contains(address.console))
		return false;
	for (std::size_t i = 0; i < controls.layout.profiles.size(); ++i)
		if (controls.layout.profiles[i].id == profile)
		{
			if (controls.layout.fixed_profiles.contains(address.console) &&
			    controls.layout.fixed_profiles.at(address.console) != i)
				return false;
			set_console_complexity(controls, address.console, i);
			impl_->refresh_controllers(address.ship);
			return true;
		}
	return false;
}

bool World::claim(std::uint64_t console, EntityId ship, const std::string &name, bool sidebar)
{
	impl_->clear_presentation_snapshots();
	if (!can_claim(console, ship, name, sidebar))
		return false;
	const auto &seat = *impl_->seat(ship, name);
	if (!impl_->ownership.claim(console, {ship, seat.id}, sidebar))
		return false;
	auto entity = impl_->entities.at(ship);
	impl_->refresh_control(ship);
	impl_->prune_displays();
	return true;
}

bool World::release(std::uint64_t console)
{
	impl_->clear_presentation_snapshots();
	auto found = impl_->ownership.claims().find(console);
	if (found == impl_->ownership.claims().end())
		return false;
	auto entity = impl_->entities.at(found->second.ship);
	std::erase_if(impl_->meta_pending,
	              [console](const auto &entry)
	              {
		              return entry.first == console;
	              });
	const auto released_ship = found->second.ship;
	std::erase_if(impl_->displays,
	              [console](const auto &entry)
	              {
		              return entry.second.claimant == console;
	              });
	impl_->ownership.release(console);
	if (entity.has<EntityControls>())
		impl_->refresh_controllers(released_ship);
	impl_->prune_displays();
	return true;
}

bool World::can_submit(std::uint64_t console, const OperatingCommand &command) const
{
	auto claim = impl_->ownership.claims().find(console);
	if (claim == impl_->ownership.claims().end() || claim->second.ship != command.target)
		return false;
	if (impl_->departed(command.target))
		return false;
	const auto address = impl_->command_address(console, command);
	if (address != claim->second && impl_->console_operator(address) != console)
		return false;
	if ((command.arguments.empty() || command.action.starts_with("display.")) && impl_->meta(console, command, false))
		return impl_->meta_pending.size() < 4096;
	if (impl_->entities.at(command.target).has<EntityControls>())
		return impl_->controller_proposal(address, command, true).has_value() &&
		       impl_->controller_pending.size() < 4096;
	return false;
}

bool World::submit(std::uint64_t console, const OperatingCommand &command)
{
	impl_->clear_presentation_snapshots();
	if (!can_submit(console, command))
		return false;
	if (impl_->meta(console, command, false))
	{
		impl_->meta_pending.emplace_back(console, command);
		return true;
	}
	if (impl_->entities.at(command.target).has<EntityControls>())
	{
		impl_->controller_pending.push_back(
		    *impl_->controller_proposal(impl_->command_address(console, command), command, true));
		return true;
	}
	return false;
}

void World::clear_claims()
{
	impl_->clear_presentation_snapshots();
	impl_->ownership.clear_claims();
	impl_->displays.clear();
	impl_->meta_pending.clear();
	impl_->controller_pending.clear();
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			impl_->refresh_controllers(id);
	impl_->knowledge.clear_control();
}
} // namespace phoenix
