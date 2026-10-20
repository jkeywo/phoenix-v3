module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.session.execution;
namespace phoenix
{
GameExecution::GameExecution(std::shared_ptr<HostAccess> world, std::map<std::string, std::string> slots)
    : world_(std::move(world)), slots_(std::move(slots))
{
	std::set<std::string> assigned;
	for (const auto &[server, slot] : slots_)
	{
		// A GM participates in replication without owning a ship or any player leases.
		if (slot.empty())
			continue;
		if (!assigned.insert(slot).second)
			throw std::runtime_error("Duplicate server slot ownership");
		auto ship = world_->authoritative_world().ship_for_slot(slot);
		if (!ship)
			throw std::runtime_error("Server slot is missing from the world");
		for (const auto &seat : world_->authoritative_world().consoles(*ship))
			leases_.push_back({{*ship, seat.id}, server});
	}
}
const ConsoleLease *GameExecution::lease(const ConsoleAddress &address) const
{
	for (const auto &lease : leases_)
		if (lease.address == address)
			return &lease;
	return nullptr;
}
const ConsoleLease *GameExecution::owned(const std::string &server, const std::string &owner, std::uint64_t route) const
{
	for (const auto &lease : leases_)
		if (lease.server == server && lease.owner == owner && lease.route == route && lease.active)
			return &lease;
	return nullptr;
}
std::vector<ConsoleSeat> GameExecution::seats(const std::string &server) const
{
	std::vector<ConsoleSeat> result;
	for (const auto &lease : leases_)
		if (lease.server == server)
			for (const auto &seat : world_->authoritative_world().consoles(lease.address.ship))
				if (seat.id == lease.address.console && !seat.human_seeking)
					result.push_back({lease.address, seat, !lease.active});
	return result;
}
bool GameExecution::can_claim(const std::string &server, const std::string &owner, std::uint64_t route,
                              const ConsoleAddress &address, bool replace) const
{
	auto seat = lease(address);
	return seat && seat->server == server && !seat->active && !owner.empty() && route && route <= 32 &&
	       !owned(server, owner, route) && (seat->owner.empty() || seat->owner == owner || replace);
}
bool GameExecution::can_command(const std::string &server, const std::string &owner, std::uint64_t route,
                                const std::string &token, std::uint64_t sequence, const OperatingCommand &command) const
{
	auto seat = owned(server, owner, route);
	return seat && seat->token == token && sequence > seat->sequence &&
	       world_->authoritative_world().can_submit(seat->claimant, command);
}
bool GameExecution::apply(const std::string &server, const Json &frame)
{
	if (!slots_.contains(server))
		return false;
	const std::string kind = frame.at("kind");
	auto &world = world_->authoritative_world();
	if (kind == "tick")
	{
		if (frame.at("tick") != world.ticks() + 1)
			return false;
		world.tick();
		checkpoint_ready_ = true;
		return true;
	}
	const std::string owner = frame.at("owner");
	const auto route = frame.value("route", std::uint64_t{1});
	if (kind == "station-claim")
	{
		ConsoleAddress address{frame.at("ship"), frame.at("seat")};
		const std::string token = frame.at("lease");
		if (!valid_hex_token(token) || !can_claim(server, owner, route, address, frame.value("replace", false)))
			return false;
		for (auto &seat : leases_)
			if (seat.address == address)
			{
				if (!world.claim(next_claimant_, address.ship, address.console))
					return false;
				seat.owner = owner;
				seat.token = token;
				seat.route = route;
				seat.claimant = next_claimant_++;
				seat.sequence = 0;
				seat.active = true;
				return true;
			}
	}
	if (kind == "station-release")
	{
		for (auto &seat : leases_)
			if (seat.server == server && seat.owner == owner && seat.route == route && seat.active &&
			    frame.at("lease") == seat.token)
			{
				world.release(seat.claimant);
				seat.active = false;
				if (frame.value("forget", false))
				{
					seat.owner.clear();
					seat.token.clear();
				}
				return true;
			}
	}
	if (kind == "command")
	{
		const auto command = decode_operating_command(frame);
		const auto sequence = frame.at("sequence").get<std::uint64_t>();
		if (!can_command(server, owner, route, frame.at("lease"), sequence, command))
			return false;
		for (auto &seat : leases_)
			if (seat.server == server && seat.owner == owner && seat.route == route && seat.active)
			{
				if (!world.submit(seat.claimant, command))
					return false;
				seat.sequence = sequence;
				checkpoint_ready_ = false;
				return true;
			}
	}
	return false;
}
void GameExecution::absent(const std::string &server)
{
	for (auto &seat : leases_)
		if (seat.server == server && seat.active)
		{
			world_->authoritative_world().release(seat.claimant);
			seat.active = false;
		}
}
std::optional<Observation> GameExecution::observe(const std::string &server, const std::string &owner,
                                                  std::uint64_t route) const
{
	auto seat = owned(server, owner, route);
	return seat ? world_->authoritative_world().observe(seat->claimant) : std::nullopt;
}
Json GameExecution::snapshot() const
{
	Json seats = Json::array();
	for (const auto &seat : leases_)
		seats.push_back({{"ship", seat.address.ship},
		                 {"seat", seat.address.console},
		                 {"server", seat.server},
		                 {"owner", seat.owner},
		                 {"lease", seat.token},
		                 {"route", seat.route},
		                 {"claimant", seat.claimant},
		                 {"sequence", seat.sequence},
		                 {"active", seat.active}});
	return {{"schema", 1}, {"next_claimant", next_claimant_}, {"seats", seats}};
}
void GameExecution::restore(const Json &state)
{
	if (state.at("schema") != 1 || state.at("seats").size() != leases_.size())
		throw std::runtime_error("Invalid Console lease snapshot");
	auto candidate = leases_;
	const auto next = state.at("next_claimant").get<std::uint64_t>();
	if (!next || next >= 9007199254740991ULL)
		throw std::runtime_error("Invalid next Console claimant");
	std::set<std::uint64_t> claimants;
	std::set<std::pair<std::string, std::pair<std::string, std::uint64_t>>> owners;
	for (std::size_t i = 0; i < candidate.size(); ++i)
	{
		auto &seat = candidate[i];
		const auto &saved = state.at("seats")[i];
		if (saved.at("ship") != seat.address.ship || saved.at("seat") != seat.address.console ||
		    saved.at("server") != seat.server)
			throw std::runtime_error("Console lease roster mismatch");
		seat.owner = saved.at("owner");
		seat.token = saved.at("lease");
		seat.route = saved.at("route");
		seat.claimant = saved.at("claimant");
		seat.sequence = saved.at("sequence");
		seat.active = saved.at("active");
		if (!seat.route || seat.route > 32 || seat.claimant >= next ||
		    (!seat.token.empty() && !valid_hex_token(seat.token)))
			throw std::runtime_error("Invalid saved Console lease");
		if (seat.active)
		{
			if (seat.owner.empty() || seat.token.empty() || !seat.claimant ||
			    !owners.insert({seat.server, {seat.owner, seat.route}}).second)
				throw std::runtime_error("Invalid active Console lease ownership");
			auto view = world_->authoritative_world().observe(seat.claimant);
			if (!claimants.insert(seat.claimant).second || !view || view->own.id != seat.address.ship ||
			    view->console != seat.address.console)
				throw std::runtime_error("Console lease and world claim disagree");
		}
	}
	leases_ = std::move(candidate);
	next_claimant_ = next;
	checkpoint_ready_ = true;
}
} // namespace phoenix
