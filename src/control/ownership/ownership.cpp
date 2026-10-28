module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.control.ownership;

namespace phoenix
{
void ConsoleOwnership::configure_ship(EntityId ship, ShipParticipation participation)
{
	participation_[ship] = std::move(participation);
}

const ConsoleParticipation *ConsoleOwnership::console(const ConsoleAddress &address) const
{
	const auto ship = participation_.find(address.ship);
	if (ship == participation_.end())
		return nullptr;
	for (const auto &console : ship->second.consoles)
		if (console.id == address.console)
			return &console;
	return nullptr;
}

bool ConsoleOwnership::can_claim(std::uint64_t claimant, const ConsoleAddress &address, bool sidebar) const
{
	if (!claimant || claims_.contains(claimant))
		return false;
	const auto *seat = console(address);
	if (!seat || (seat->human_seeking && !sidebar))
		return false;
	if (sidebar && (!seat->human_seeking || !seat->viewscreen || operator_for(address)))
		return false;
	return std::none_of(claims_.begin(), claims_.end(),
	                    [&](const auto &entry)
	                    {
		                    return entry.second == address;
	                    });
}

bool ConsoleOwnership::claim(std::uint64_t claimant, const ConsoleAddress &address, bool sidebar)
{
	if (!can_claim(claimant, address, sidebar))
		return false;
	claims_.emplace(claimant, address);
	return true;
}

std::optional<ConsoleAddress> ConsoleOwnership::release(std::uint64_t claimant)
{
	const auto found = claims_.find(claimant);
	if (found == claims_.end())
		return std::nullopt;
	const auto address = found->second;
	claims_.erase(found);
	return address;
}

std::vector<std::uint64_t> ConsoleOwnership::release_ship(EntityId ship)
{
	std::vector<std::uint64_t> released;
	for (const auto &[claimant, address] : claims_)
		if (address.ship == ship)
			released.push_back(claimant);
	for (const auto claimant : released)
		claims_.erase(claimant);
	return released;
}

void ConsoleOwnership::clear_claims()
{
	claims_.clear();
}

bool ConsoleOwnership::empty() const
{
	return claims_.empty();
}

std::optional<ConsoleAddress> ConsoleOwnership::claim_for(std::uint64_t claimant) const
{
	const auto found = claims_.find(claimant);
	return found == claims_.end() ? std::optional<ConsoleAddress>{} : found->second;
}

std::uint64_t ConsoleOwnership::operator_for(const ConsoleAddress &address) const
{
	const auto *seat = console(address);
	if (!seat || is_away(address))
		return 0;
	for (const auto &[claimant, claim] : claims_)
		if (claim == address)
			return claimant;
	// The default supplemental Viewscreen Console is reserved for the host.
	if (participation_.at(address.ship).sidebar_default == address.console)
		return 0;
	if (seat->human_seeking)
		for (const auto &station : seat->human_preferences)
			for (const auto &[claimant, claim] : claims_)
				if (claim.ship == address.ship && !is_away(claim))
					if (const auto *claimed = console(claim); claimed && claimed->station == station)
						return claimant;
	return 0;
}

bool ConsoleOwnership::owns_command(std::uint64_t claimant, const ConsoleAddress &address) const
{
	const auto claim = claim_for(claimant);
	return claim && claim->ship == address.ship && (*claim == address || operator_for(address) == claimant);
}

bool ConsoleOwnership::is_away(const ConsoleAddress &address) const
{
	const auto found = away_.find(address);
	return found != away_.end() && found->second;
}

void ConsoleOwnership::set_away(const ConsoleAddress &address, bool away)
{
	if (!console(address))
		throw std::runtime_error("Unknown Console participation address");
	away_[address] = away;
}

const std::map<std::uint64_t, ConsoleAddress> &ConsoleOwnership::claims() const
{
	return claims_;
}

const std::map<ConsoleAddress, bool> &ConsoleOwnership::away() const
{
	return away_;
}
} // namespace phoenix
