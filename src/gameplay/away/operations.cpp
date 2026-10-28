module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.gameplay.away.operations;

namespace phoenix
{
AwayOperations::AwayOperations() = default;
AwayOperations::~AwayOperations() = default;
AwayOperations::AwayOperations(const AwayOperations &) = default;
AwayOperations &AwayOperations::operator=(const AwayOperations &) = default;
AwayOperations::AwayOperations(AwayOperations &&) noexcept = default;
AwayOperations &AwayOperations::operator=(AwayOperations &&) noexcept = default;
const std::vector<AwayDefinition> &AwayOperations::definitions() const
{
	return definitions_;
}
void AwayOperations::configure_definitions(std::vector<AwayDefinition> definitions)
{
	definitions_ = std::move(definitions);
}
void AwayOperations::initialize_missions()
{
	missions_.resize(definitions_.size());
}
void AwayOperations::append_definition(const AwayDefinition &definition)
{
	definitions_.push_back(definition);
	missions_.emplace_back();
}
std::span<AwayMission> AwayOperations::missions()
{
	return {missions_.data(), missions_.size()};
}
std::span<const AwayMission> AwayOperations::missions() const
{
	return {missions_.data(), missions_.size()};
}
AwayMission &AwayOperations::mission(std::size_t index)
{
	return missions_.at(index);
}
const AwayMission &AwayOperations::mission(std::size_t index) const
{
	return missions_.at(index);
}
std::uint64_t AwayOperations::sequence() const
{
	return sequence_;
}
std::uint64_t AwayOperations::next_sequence()
{
	return ++sequence_;
}
void AwayOperations::observe_sequence(std::uint64_t sequence)
{
	sequence_ = std::max(sequence_, sequence);
}
EntityId AwayOperations::origin(const AwayDefinition &definition, const std::map<std::string, EntityId> &slots) const
{
	return definition.origin ? definition.origin : slots.contains(definition.slot) ? slots.at(definition.slot) : 0;
}
bool AwayOperations::officer_away(EntityId ship, const std::string &officer,
                                  const std::map<std::string, EntityId> &slots) const
{
	for (std::size_t i = 0; i < missions_.size(); ++i)
		if (origin(definitions_[i], slots) == ship && missions_[i].absent())
		{
			const auto &mission = missions_[i];
			if (std::find(mission.accompanying.begin(), mission.accompanying.end(), officer) !=
			    mission.accompanying.end())
				return true;
			for (const auto &[slot, asset] : mission.assignments)
				if (asset == "officer:" + officer)
					return true;
		}
	return false;
}
bool AwayOperations::asset_assigned(EntityId ship, const std::string &asset,
                                    const std::map<std::string, EntityId> &slots) const
{
	for (std::size_t i = 0; i < missions_.size(); ++i)
		if (origin(definitions_[i], slots) == ship)
		{
			const auto &mission = missions_[i];
			for (const auto &[slot, assignment] : mission.assignments)
				if (assignment == asset)
					return true;
			if (asset.starts_with("officer:") && std::find(mission.accompanying.begin(), mission.accompanying.end(),
			                                               asset.substr(8)) != mission.accompanying.end())
				return true;
		}
	return false;
}
bool AwayOperations::craft_reserved(EntityId home, EntityId craft, const std::string &except,
                                    const std::map<std::string, EntityId> &slots) const
{
	for (std::size_t i = 0; i < missions_.size(); ++i)
	{
		const auto &definition = definitions_[i];
		const auto &mission = missions_[i];
		if (origin(definition, slots) != home || definition.id == except)
			continue;
		if ((!mission.transport_released && mission.transport == "craft:" + std::to_string(craft) &&
		     mission.phase != AwayPhase::Lost &&
		     !(mission.phase == AwayPhase::Recovered && !mission.outcome.empty())) ||
		    std::any_of(mission.escorts.begin(), mission.escorts.end(),
		                [&](const auto &pair)
		                {
			                return pair.second == craft;
		                }))
			return true;
	}
	return false;
}
bool AwayOperations::defender_reserved(EntityId target, const std::string &asset) const
{
	if (!asset.starts_with("team:"))
		return false;
	for (std::size_t i = 0; i < definitions_.size(); ++i)
	{
		const auto &definition = definitions_[i];
		const auto &mission = missions_[i];
		if (!definition.boarding || definition.target != target || !mission.deployed() ||
		    mission.boarding.encounter.empty() || mission.boarding.encounter_done)
			continue;
		const auto &encounter = definition.boarding->encounters.at(mission.boarding.encounter);
		if (std::find(encounter.defenders.begin(), encounter.defenders.end(), asset.substr(5)) !=
		    encounter.defenders.end())
			return true;
	}
	return false;
}
bool AwayOperations::conversation_requested(const std::string &id) const
{
	for (std::size_t i = 0; i < definitions_.size(); ++i)
		if (definitions_[i].boarding && definitions_[i].conversation == id)
			return missions_[i].phase != AwayPhase::Hidden && missions_[i].phase != AwayPhase::Available;
	return false;
}
std::map<std::pair<EntityId, std::string>, std::size_t>
AwayOperations::active_transfers(const std::map<std::string, EntityId> &slots) const
{
	std::map<std::pair<EntityId, std::string>, std::size_t> active;
	for (std::size_t i = 0; i < missions_.size(); ++i)
	{
		const auto &mission = missions_[i];
		if (mission.phase == AwayPhase::Outbound || mission.phase == AwayPhase::Inbound)
		{
			auto key = std::pair{origin(definitions_[i], slots), mission.transport};
			if (!active.contains(key) || mission.sequence < missions_[active.at(key)].sequence)
				active[key] = i;
		}
	}
	return active;
}
void AwayOperations::mark_deployed(AwayMission &mission, std::optional<std::int64_t> boarding_entry)
{
	if (boarding_entry)
	{
		mission.boarding.location = *boarding_entry;
		mission.boarding.travel = 0;
		mission.boarding.edge_to = -1;
	}
	mission.phase = AwayPhase::Deployed;
	mission.recovered = false;
	mission.ever_deployed = true;
	mission.transfer = 0;
	for (const auto &[slot, asset] : mission.assignments)
		mission.participants.insert(asset);
	for (const auto &officer : mission.accompanying)
		mission.participants.insert("officer:" + officer);
	++mission.revision;
}
void AwayOperations::finish_recovery(AwayMission &mission)
{
	mission.aboard_craft = false;
	mission.phase = AwayPhase::Recovered;
	mission.recovered = true;
	mission.transport_released = false;
	mission.transfer = 0;
	mission.assignments.clear();
	mission.accompanying.clear();
	mission.care.clear();
	++mission.revision;
}
std::map<std::string, std::string> AwayOperations::personnel_dispositions(
    EntityId ship, const std::map<std::string, EntityId> &slots, const std::map<std::string, std::string> &postings,
    const std::set<std::string> &lost, std::span<const std::string> dead_officers) const
{
	std::map<std::string, std::string> dispositions;
	for (std::size_t i = 0; i < missions_.size(); ++i)
		if (origin(definitions_[i], slots) == ship)
		{
			const auto &mission = missions_[i];
			for (const auto &asset : mission.participants)
				dispositions.try_emplace(asset, "recovered");
			if (mission.absent())
			{
				for (const auto &[slot, asset] : mission.assignments)
					dispositions[asset] = mission.boarding.prize_crew ? "prize_crew" : "deployed";
				for (const auto &officer : mission.accompanying)
					dispositions["officer:" + officer] = mission.boarding.prize_crew ? "prize_crew" : "deployed";
			}
		}
	for (const auto &[asset, status] : postings)
		dispositions[asset] = status;
	// Loss is permanent and takes precedence over history or a current manifest.
	for (const auto &asset : lost)
		dispositions[asset] = "lost";
	for (const auto &officer : dead_officers)
		if (dispositions.contains("officer:" + officer))
			dispositions["officer:" + officer] = "dead";
	return dispositions;
}
AwayLossDecision AwayOperations::loss_decision(const AwayMission &mission, const AwayLossFacts &facts) const
{
	if (!facts.origin_assigned || mission.phase == AwayPhase::Lost ||
	    (mission.phase == AwayPhase::Inbound && mission.aboard_craft && !facts.lost_transport) ||
	    (!facts.target_destroyed && !facts.lost_transport))
		return AwayLossDecision::None;
	if (((mission.deployed() || mission.phase == AwayPhase::Captured) && facts.target_destroyed) ||
	    facts.lost_transport ||
	    (mission.aboard_craft && facts.transport_present && facts.torpedo_launched && facts.target_destroyed))
		return AwayLossDecision::Lost;
	if (mission.phase == AwayPhase::Outbound)
	{
		if (mission.aboard_craft && facts.transport_present)
			return AwayLossDecision::Pickup;
		return AwayLossDecision::Reopen;
	}
	return AwayLossDecision::None;
}
Json AwayOperations::mission_record(std::size_t index) const
{
	Json row = missions_[index];
	row["schema"] = 11;
	row["id"] = definitions_[index].id;
	row["continuation"] = state_tree::save(definitions_[index].program, missions_[index].continuation);
	return row;
}
void AwayOperations::restore_order(const Json &rows)
{
	if (!rows.is_array() || rows.size() != definitions_.size())
		throw std::runtime_error("Invalid saved away missions");
	// Dynamic target templates can have been discovered over several spawn ticks.
	// Restore their saved ordering by stable identity, rather than reconstruction order.
	std::vector<AwayDefinition> ordered;
	std::set<std::string> identities;
	for (const auto &row : rows)
	{
		const auto id = row.at("id").get<std::string>();
		const auto found = std::find_if(definitions_.begin(), definitions_.end(),
		                                [&](const auto &definition)
		                                {
			                                return definition.id == id;
		                                });
		if (!identities.insert(id).second || found == definitions_.end())
			throw std::runtime_error("Away mission identity mismatch");
		ordered.push_back(*found);
	}
	definitions_ = std::move(ordered);
	sequence_ = 0;
}
} // namespace phoenix
