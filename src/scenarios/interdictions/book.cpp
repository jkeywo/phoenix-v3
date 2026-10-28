module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.scenario.interdictions;
import phoenix.state_tree;
namespace phoenix
{
void InterdictionBook::configure(std::vector<InterdictionDefinition> definitions)
{
	definitions_ = std::move(definitions);
	states_.clear();
	states_.resize(definitions_.size());
}
std::size_t InterdictionBook::size() const
{
	return states_.size();
}
const InterdictionDefinition &InterdictionBook::definition(std::size_t index) const
{
	return definitions_.at(index);
}
const InterdictionState &InterdictionBook::state(std::size_t index) const
{
	return states_.at(index);
}

bool InterdictionBook::participant(std::size_t index, EntityId id) const
{
	const auto &ships = states_.at(index).ships;
	return std::find(ships.begin(), ships.end(), id) != ships.end();
}
bool InterdictionBook::docking_permission(EntityId ship, EntityId target) const
{
	for (std::size_t i = 0; i < states_.size(); ++i)
	{
		const auto &s = states_[i];
		if (definitions_[i].target == target && s.boarder == ship && !s.captured && !s.finished &&
		    (s.posture == "truce" || s.posture == "hostile") && !s.blocked)
			return true;
	}
	return false;
}
bool InterdictionBook::capture_receipt(EntityId target, EntityId owner) const
{
	for (std::size_t i = 0; i < states_.size(); ++i)
		if (definitions_[i].target == target && states_[i].captured && participant(i, owner))
			return true;
	return false;
}
bool InterdictionBook::departure_members_allowed(std::size_t index, EntityId leader, EntityId member,
                                                 EntityId target_owner) const
{
	const auto &s = states_.at(index);
	const auto target = definitions_.at(index).target;
	return s.captured && participant(index, target_owner) && (leader == target || participant(index, leader)) &&
	       (member == target || participant(index, member));
}
bool InterdictionBook::obsolete_departure(std::size_t index, EntityId leader, const std::set<EntityId> &group,
                                          EntityId target_owner) const
{
	return states_.at(index).captured && !participant(index, target_owner) && participant(index, leader) &&
	       std::all_of(group.begin(), group.end(),
	                   [&](auto id)
	                   {
		                   return participant(index, id);
	                   });
}
void InterdictionBook::record_attack(std::size_t index, EntityId source_owner, EntityId player_owner, EntityId target)
{
	if (source_owner == player_owner && participant(index, target))
		states_.at(index).attacked = true;
}
void InterdictionBook::record_insertion(std::size_t index, EntityId ship, EntityId player, EntityId target)
{
	if (states_.at(index).posture == "truce" && definitions_.at(index).target == target && ship == player)
		states_[index].reinforced = true;
}
Json InterdictionBook::save() const
{
	Json rows = Json::array();
	for (std::size_t i = 0; i < states_.size(); ++i)
	{
		const auto &s = states_[i];
		rows.push_back({{"id", definitions_[i].id},
		                {"ships", s.ships},
		                {"boarder", s.boarder},
		                {"posture", s.posture},
		                {"conversation", s.conversation},
		                {"progress", s.progress},
		                {"attacked", s.attacked},
		                {"reinforced", s.reinforced},
		                {"captured", s.captured},
		                {"blocked", s.blocked},
		                {"finished", s.finished},
		                {"effects", s.effects},
		                {"plea_messages", s.plea_messages},
		                {"continuation", state_tree::save(definitions_[i].program, s.continuation)}});
	}
	return rows;
}
void InterdictionBook::restore(const Json &rows, const InterdictionRestoreFacts &facts)
{
	if (!rows.is_array() || rows.size() != states_.size())
		throw std::runtime_error("Invalid interdiction continuation");
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &j = rows[i];
		auto &s = states_[i];
		const auto &d = definitions_[i];
		if (j.at("id") != d.id)
			throw std::runtime_error("Interdiction identity mismatch");
		s.ships = j.at("ships").get<std::vector<EntityId>>();
		s.boarder = j.at("boarder");
		s.posture = j.at("posture");
		s.conversation = j.at("conversation");
		s.progress = j.at("progress");
		s.attacked = j.at("attacked");
		s.reinforced = j.at("reinforced");
		s.captured = j.at("captured");
		s.blocked = j.at("blocked");
		s.finished = j.at("finished");
		s.effects = j.at("effects").get<std::set<std::string>>();
		s.plea_messages = j.value("plea_messages", std::vector<std::uint64_t>{});
		if (s.ships.size() > 8 || s.progress < 0 || s.progress > d.takeover_ticks ||
		    s.effects.size() > effect_receipt_limit ||
		    (s.posture != "watch" && s.posture != "hostile" && s.posture != "truce") ||
		    (s.boarder && std::find(s.ships.begin(), s.ships.end(), s.boarder) == s.ships.end()) ||
		    (s.ships.empty() && (s.boarder || s.conversation || s.progress || s.captured)) ||
		    s.plea_messages.size() > d.pleas.size() ||
		    (s.conversation && !facts.conversations.contains(s.conversation)))
			throw std::runtime_error("Invalid interdiction state");
		if ((s.captured && !facts.captured_targets.contains(d.target)) ||
		    (s.conversation && facts.conversations.at(s.conversation).program != d.dialogue))
			throw std::runtime_error("Interdiction ownership or conversation receipt mismatch");
		for (const auto id : s.ships)
			if (!facts.entities.contains(id))
				throw std::runtime_error("Unknown interdiction ship");
		for (const auto message : s.plea_messages)
			if (!s.conversation || !facts.conversations.at(s.conversation).messages.contains(message))
				throw std::runtime_error("Unknown takeover plea receipt");
		auto continuation = j.at("continuation");
		if (continuation.at("program") != d.program.signature)
		{
			const auto legacy = facts.legacy_signatures.find(d.id);
			if (legacy != facts.legacy_signatures.end() && continuation.at("program") == legacy->second)
				continuation["program"] = d.program.signature;
		}
		s.continuation = state_tree::load(d.program, continuation);
	}
}
std::vector<InterdictionShipRestore>
InterdictionBook::restore_ships(const Json &rows, const Json &bodies,
                                const std::map<std::string, std::string> &slot_fittings,
                                const std::set<EntityId> &existing) const
{
	if (!rows.is_array() || rows.size() != definitions_.size())
		throw std::runtime_error("Interdiction roster mismatch");
	std::vector<InterdictionShipRestore> result;
	auto seen = existing;
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &d = definitions_[i];
		const auto &fit = slot_fittings.at(d.slot);
		const auto &ships = d.variants.at(d.variants.contains(fit) ? fit : "default");
		const auto &ids = rows[i].at("ships");
		if (!ids.empty() && ids.size() != ships.size())
			throw std::runtime_error("Invalid interdiction fleet size");
		for (std::size_t n = 0; n < ids.size(); ++n)
		{
			const auto id = ids[n].get<EntityId>();
			const auto row = std::find_if(bodies.begin(), bodies.end(),
			                              [&](const auto &b)
			                              {
				                              return b.at("body").at("id") == id;
			                              });
			if (!id || seen.contains(id) || row == bodies.end() ||
			    row->at("body").at("definition") != ships[n].definition)
				throw std::runtime_error("Invalid interdiction ship identity");
			seen.insert(id);
			result.push_back({id, ships[n].definition});
		}
	}
	return result;
}
} // namespace phoenix
