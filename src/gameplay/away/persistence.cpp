module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
module phoenix.gameplay.away.runtime;
import phoenix.gameplay.away.crew;
import phoenix.state_tree;
namespace phoenix
{
AwayRestoreChecks::~AwayRestoreChecks() = default;
namespace
{
const PersonnelDuty *restore_team_duty(const PersonnelDefinition &personnel, const std::string &team)
{
	for (const auto &duty : personnel.duties)
		if (duty.team == team)
			return &duty;
	return nullptr;
}
const TeamOrderDefinition *medical_order(const CombatMechanism &mechanism, const std::string &id)
{
	for (const auto &order : mechanism.definition.get<TeamDefinition>().orders)
		if (order.id == id)
			return &order;
	return nullptr;
}
const ConsoleSpec *restore_seat(const std::vector<ConsoleSpec> &seats, const std::string &name)
{
	for (const auto &seat : seats)
		if (seat.id == name || (name.empty() && seats.size() == 1))
			return &seat;
	return nullptr;
}
} // namespace

Json AwayRuntime::save(const AwayObservation &observation) const
{
	Json out = Json::array();
	for (std::size_t i = 0; i < missions().size(); ++i)
	{
		Json row = mission_record(i);
		for (const auto &[team, care] : missions()[i].care)
		{
			const auto &equipment = observation.physical.ship(origin(definitions()[i], observation.slots)).team(team);
			const auto *order = medical_order(equipment, care.order);
			if (!order)
				throw std::runtime_error("Missing away medical order");
			row["care"][team]["continuation"] = state_tree::save(order->program, care.continuation);
		}
		out.push_back(row);
	}
	return out;
}
void AwayRuntime::restore(const Json &rows, const AwayRestoreContext &context)
{
	const auto &observation = context.observation;
	restore_order(rows);
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &d = definitions()[i];
		const auto &record = rows[i];
		if (record.at("schema") != 11)
			throw std::runtime_error("Saved away mission schema retired; start a new session (schema 11 required)");
		const auto &care_record = record.at("care");
		if (!care_record.is_object() || care_record.size() > 16)
			throw std::runtime_error("Invalid away care roster");
		auto m = record.get<AwayMission>();
		if (m.flags.size() > 64 || m.saved_progress.size() > 256 || m.crew_deaths < 0 || m.crew_deaths > 4096 ||
		    (!m.work_capability.empty() &&
		     std::none_of(d.program.registry.observations.begin(), d.program.registry.observations.end(),
		                  [&](const auto &o)
		                  {
			                  return o.name == "team_capabilities." + m.work_capability;
		                  })))
			throw std::runtime_error("Invalid away job continuation");
		for (const auto &[key, progress] : m.saved_progress)
			if (key.empty() || key.size() > 256 || progress < 0 || progress > 1728000000)
				throw std::runtime_error("Invalid saved away job progress");
		if (m.entry_compartment < 0 ||
		    (observation.physical.ship(d.target).layout().structure &&
		     m.entry_compartment >= static_cast<std::int64_t>(
		                                observation.physical.ship(d.target).layout().structure->compartments.size())))
			throw std::runtime_error("Invalid away entry compartment");
		if (record.contains("deployed"))
			throw std::runtime_error("Inconsistent saved deployment");
		if (!m.repair_compartment.empty() && !d.repair_compartments.contains(m.repair_compartment))
			throw std::runtime_error("Invalid saved repair target");
		const auto ship = origin(d, observation.slots);
		if (m.escorts.size() > 8 ||
		    (ship && m.authorised_departments.size() > observation.physical.ship(ship).layout().consoles.size()) ||
		    m.supply_quantity < 0 || m.supply_quantity > 1000000000 || (m.supply_job && !d.supply))
			throw std::runtime_error("Invalid mission contributions");
		for (const auto &console : m.authorised_departments)
			if (!ship || !restore_seat(observation.physical.ship(ship).layout().consoles, console))
				throw std::runtime_error("Invalid contributing department");
		std::set<EntityId> escorts;
		for (const auto &[slot, id] : m.escorts)
		{
			const auto fit = std::find_if(d.craft_slots.begin(), d.craft_slots.end(),
			                              [&](const auto &s)
			                              {
				                              return s.id == slot && s.role == "escort";
			                              });
			if (fit == d.craft_slots.end() || !observation.physical.is_craft(id) ||
			    observation.physical.craft(id).state().home != ship ||
			    observation.physical.ship(id).layout().craft->role != fit->craft_role || !escorts.insert(id).second ||
			    m.transport == "craft:" + std::to_string(id))
				throw std::runtime_error("Invalid saved mission escort");
		}

		if (m.revision < 1 || m.revision > 9007199254740991ULL || m.sequence > 9007199254740991ULL || m.progress < 0 ||
		    m.work < 0 || m.progress > m.work || m.work > 1728000000 || m.transfer < 0 ||
		    m.effects.size() > effect_receipt_limit || m.assignments.size() > 16 || m.participants.size() > 256 ||
		    (!m.ever_deployed && !m.participants.empty()))
			throw std::runtime_error("Invalid away continuation");
		for (const auto &asset : m.participants)
		{
			bool valid = asset.starts_with("team:") &&
			             restore_team_duty(observation.physical.ship(ship).layout().personnel, asset.substr(5));
			if (asset.starts_with("officer:"))
				for (const auto &o : observation.physical.ship(ship).layout().personnel.officers)
					valid |= asset == "officer:" + o.id;
			if (!valid)
				throw std::runtime_error("Invalid away participant");
		}
		if (!m.transport.empty() &&
		    (!ship || (!observation.physical.ship(ship).layout().displacements.contains(m.transport) &&
		               !observation.physical.transport(ship, m.transport))))
			throw std::runtime_error("Invalid saved transport");
		if ((m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound || m.deployed()) && m.transport.empty())
			throw std::runtime_error("Missing saved transport");
		if ((m.phase == AwayPhase::Recovered && !m.recovered) || m.accompanying.size() > 128 ||
		    m.checkpoint.size() > 256 || m.work_key.size() > 256 || m.outcome.size() > 512)
			throw std::runtime_error("Inconsistent away phase");
		if (!m.transport.empty() && m.transfer >= transfer_ticks(d, m, observation))
			throw std::runtime_error("Invalid transfer progress");
		std::set<std::string> assets;
		for (const auto &[key, asset] : m.assignments)
		{
			if (!assets.insert(asset).second)
				throw std::runtime_error("Duplicate away asset");
			auto s = std::find_if(d.slots.begin(), d.slots.end(),
			                      [&](const auto &s)
			                      {
				                      return s.id == key;
			                      });
			if (s == d.slots.end() || !slot_accepts(ship, *s, asset, observation) ||
			    reserved(ship, asset, context.reservations))
				throw std::runtime_error("Invalid saved assignment");
		}
		std::set<std::string> attached;
		for (const auto &key : m.accompanying)
		{
			if (!attached.insert(key).second || assets.contains("officer:" + key) ||
			    reserved(ship, "officer:" + key, context.reservations))
				throw std::runtime_error("Duplicate accompanying officer");
			bool valid = false;
			const auto &pd = observation.physical.ship(ship).layout().personnel;
			const auto &ps = observation.physical.ship(ship).personnel();
			for (std::size_t j = 0; j < pd.officers.size(); ++j)
				if (pd.officers[j].id == key)
					for (const auto &[slot, asset] : m.assignments)
						if (asset.starts_with("team:") &&
						    ps.officers[j].duty ==
						        restore_team_duty(observation.physical.ship(ship).layout().personnel, asset.substr(5))
						            ->id)
							valid = true;
			if (!valid)
				throw std::runtime_error("Invalid accompanying officer");
		}
		if (ship)
		{
			const auto &pd = observation.physical.ship(ship).layout().personnel;
			const auto &ps = observation.physical.ship(ship).personnel();
			for (std::size_t j = 0; j < pd.officers.size(); ++j)
				for (const auto &[slot, asset] : m.assignments)
				{
					if (asset.starts_with("team:") &&
					    ps.officers[j].duty ==
					        restore_team_duty(observation.physical.ship(ship).layout().personnel, asset.substr(5))
					            ->id &&
					    !attached.contains(pd.officers[j].id))
						throw std::runtime_error("Missing accompanying officer");
					if (asset == "officer:" + pd.officers[j].id && !ps.officers[j].duty.empty())
						throw std::runtime_error("Deployed officer still assigned aboard");
				}
		}
		for (auto &[team, care] : m.care)
		{
			bool assigned = false;
			for (const auto &[slot, asset] : m.assignments)
				if (asset == "team:" + team)
					assigned = true;
			const bool equipment = observation.physical.ship(ship).team_present(team);
			if (!assigned || !equipment)
				throw std::runtime_error("Orphaned away care");
			const auto *order = medical_order(observation.physical.ship(ship).team(team), care.order);
			const auto &pd = observation.physical.ship(d.target).layout().personnel;
			if (!order || order->target != "patients" || care.location < 0 ||
			    care.location >= static_cast<std::int64_t>(pd.compartments.size()) || care.travel < 0 ||
			    (care.travel > 0) != (care.edge_to >= 0))
				throw std::runtime_error("Invalid away care location/order");
			if (care.travel)
			{
				bool link = false;
				for (const auto &l : pd.links)
					if (((l.from == care.location && l.to == care.edge_to) ||
					     (l.to == care.location && l.from == care.edge_to)) &&
					    care.travel <= l.ticks)
						link = true;
				if (!link)
					throw std::runtime_error("Invalid away care route");
			}
			const auto &actions = order->program.registry.actions;
			if (!care.activity.empty() && std::none_of(actions.begin(), actions.end(),
			                                           [&](const auto &action)
			                                           {
				                                           return action.name == care.activity;
			                                           }))
				throw std::runtime_error("Invalid away clinical activity");
			care.continuation = state_tree::load(order->program, care_record.at(team).at("continuation"));
		}
		context.checks.boarding(d, m);
		m.continuation = state_tree::load(d.program, rows[i].at("continuation"));
		observe_sequence(m.sequence);
		missions()[i] = std::move(m);
	}
}
bool AwayRuntime::reserved(EntityId ship, const std::string &asset, const AwayReservations &context) const
{
	if (context.postings.contains(ship) && context.postings.at(ship).contains(asset) &&
	    context.postings.at(ship).at(asset).status == "prize_crew")
		return true;
	if (defender_reserved(ship, asset))
		return true;
	if (context.ledger.lost.contains(ship) && context.ledger.lost.at(ship).contains(asset))
		return true;
	return asset_assigned(ship, asset, context.slots);
}
bool AwayRuntime::slot_accepts(EntityId ship, const AwaySlot &s, const std::string &asset,
                               const AwayObservation &observation) const
{
	const TeamDefinition *definition_team = nullptr;
	bool duty = false;
	if (s.kind == "team" && asset.starts_with("team:"))
	{
		if (observation.physical.ship(ship).team_present(asset.substr(5)))
		{
			duty = restore_team_duty(observation.physical.ship(ship).layout().personnel, asset.substr(5)) != nullptr;
			if (duty && s.trait.empty())
				definition_team =
				    &observation.physical.ship(ship).team(asset.substr(5)).definition.get<TeamDefinition>();
		}
	}
	return away_crew_slot_accepts(s, asset, observation.physical.ship(ship).layout().personnel, definition_team, duty);
}
} // namespace phoenix
