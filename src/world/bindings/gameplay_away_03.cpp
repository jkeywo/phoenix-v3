module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::away_board(const ConsoleAddress &a) const -> Json
{
	Json out = Json::array();
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		const auto &d = away_operations.definitions()[i];
		const auto &m = away_operations.missions()[i];
		if (away_ship(d) != a.ship || m.phase == AwayPhase::Hidden)
			continue;
		Json row{{"id", d.id},
		         {"title", d.title},
		         {"phase", m.phase},
		         {"result", m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Lost ? m.outcome : std::string{}},
		         {"revision", m.revision},
		         {"transport", m.transport},
		         {"target", d.target},
		         {"dispatch", away_dispatch(a, d.dispatch)},
		         {"blocker", away_blocker(i)},
		         {"ship_transport_reason", d.allow_ship_transport ? std::string{}
		                                   : d.ship_transport_reason.empty()
		                                       ? std::string{"Ship transport disabled by mission"}
		                                       : d.ship_transport_reason},
		         {"slots", Json::array()},
		         {"transports", Json::array()},
		         {"accompanying", m.accompanying}};
		row["owner"] = enabled_system_owner(a.ship, away_dispatch_system(a.ship, d.dispatch)).console;
		row["dispatch_system"] = away_dispatch_system(a.ship, d.dispatch);
		row["automatic_dispatch"] =
		    !d.supply && d.allow_ship_transport && station_stars({a.ship, row.at("owner").get<std::string>()}) == 1;
		bool owns_assets = false;
		for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
			if (controller.definition.console == a.console)
				for (const auto &ch : controller.definition.commands)
					for (const auto consumer : ch.consumers)
						owns_assets |=
						    definition(a.ship).fitted->values.at(consumer).kind == OperatingSystem::Team ||
						    definition(a.ship).fitted->values.at(consumer).kind == OperatingSystem::ShuttleBay;
		row["can_contribute"] =
		    owns_assets && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered) && m.outcome.empty();
		row["contributing"] = m.authorised_departments.contains(a.console);
		row["transport_owned"] =
		    !m.transport.empty() && row["can_contribute"] == true && away_asset_owned(a, "transport:" + m.transport) &&
		    manual_workload(a, "transport") &&
		    (station_stars(a) >= 3 ||
		     entities.at(a.ship).get<EntityControls>().layout.manual_workloads.contains(a.console));
		row["craft_slots"] = Json::array();
		for (const auto &slot : d.craft_slots)
			if (slot.role == "escort")
			{
				const auto assigned =
				    m.escorts.contains(slot.id) ? "craft:" + std::to_string(m.escorts.at(slot.id)) : std::string{};
				Json r{{"id", slot.id},
				       {"role", slot.role},
				       {"required", slot.required},
				       {"assigned", assigned},
				       {"owned", !assigned.empty() && away_asset_owned(a, assigned) && manual_workload(a, "transport")},
				       {"lost", m.flags.contains("escort_lost." + slot.id)},
				       {"choices", Json::array()}};
				if (row["can_contribute"] == true && manual_workload(a, "transport"))
					for (const auto &[id, e] : craft_rows())
						if (e.get<CraftState>().home == a.ship && definition(id).craft->role == slot.craft_role &&
						    away_asset_owned(a, "craft:" + std::to_string(id)) && craft_available(id, d.id))
							r["choices"].push_back("craft:" + std::to_string(id));
				row["craft_slots"].push_back(r);
			}
		if (d.supply)
		{
			const auto manifest = !m.supply_job ? supply_manifest(i) : std::optional<CargoDelivery>{};
			const CargoDelivery *job = nullptr;
			for (const auto &candidate : cargo_deliveries)
				if (candidate.id == m.supply_job)
					job = &candidate;
			row["supply"] = {{"material", d.supply->material},
			                 {"payment", d.supply->payment},
			                 {"give", d.supply->give},
			                 {"pay", d.supply->pay},
			                 {"quantity", job        ? job->amount
			                              : manifest ? manifest->amount
			                                         : 0},
			                 {"payment_quantity", job        ? job->payment_amount
			                                      : manifest ? manifest->payment_amount
			                                                 : 0},
			                 {"phase", job ? job->report : "preparation"},
			                 {"delivered", job ? job->delivered : 0},
			                 {"manual", station_stars(a) >= 3 && away_dispatch(a, d.dispatch) && !m.supply_job},
			                 {"quantities", Json::array({0})}};
			if (manifest && row["supply"]["manual"] == true)
			{
				for (const auto q :
				     {std::max(d.supply->give, manifest->amount - d.supply->give), manifest->amount + d.supply->give})
					if (const auto chosen = supply_manifest(i, q); chosen && chosen->amount == q)
						row["supply"]["quantities"].push_back(q);
			}
			if (!m.supply_job && !manifest && !m.transport.empty())
				row["blocker"] = "No compatible supply manifest: check stock, payment, capacity and docking";
		}
		if (d.boarding && m.boarding.reported)
			row["boarding_check"] = {{"chance_permille", m.boarding.chance},
			                         {"defenders", m.boarding.defenders},
			                         {"encounter", m.boarding.encounter},
			                         {"factors", m.boarding.factors}};
		if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
		{
			if (!m.transport.empty() && !away_staged(a.ship, m))
				row["blocker"] = "Personnel moving to transport";
			for (const auto &slot : d.slots)
				if (slot.required && !m.assignments.contains(slot.id))
					row["blocker"] = "Mandatory assignments required";
		}
		for (const auto &[key, t] : definition(a.ship).displacements)
			if ((m.outcome.empty() || m.phase == AwayPhase::Deployed) && away_asset_owned(a, "transport:" + key))
				row["transports"].push_back(key);
		for (const auto &[key, bay] : definition(a.ship).craft_bays)
			if (bay.legacy_transport && away_asset_owned(a, "transport:" + key) &&
			    canonical_away_transport(a.ship, key, d.id) != key)
				row["transports"].push_back(key);
		if (m.outcome.empty() && d.allow_ship_transport && away_ship_transport_owned(a))
			row["transports"].push_back("ship");
		for (const auto &[craft, e] : craft_rows())
			if (e.get<CraftState>().home == a.ship &&
			    (definition(craft).craft->role == "shuttle" || definition(craft).craft->role == "boarding_torpedo") &&
			    away_asset_owned(a, "transport:craft:" + std::to_string(craft)) && craft_available(craft, d.id) &&
			    std::all_of(d.craft_slots.begin(), d.craft_slots.end(),
			                [&](const auto &slot)
			                {
				                return slot.role != "transport" || slot.craft_role.empty() ||
				                       slot.craft_role == definition(craft).craft->role;
			                }))
				row["transports"].push_back("craft:" + std::to_string(craft));
		for (const auto &s : d.slots)
		{
			const auto assigned = m.assignments.contains(s.id) ? m.assignments.at(s.id) : std::string{};
			Json r{{"id", s.id},
			       {"required", s.required},
			       {"assigned", assigned},
			       {"owned", !assigned.empty() && away_asset_owned(a, assigned) &&
			                     (station_stars(a) >= 3 || !assigned.starts_with("officer:"))},
			       {"choices", Json::array()}};

			if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
			{
				for (const auto &duty : definition(a.ship).personnel.duties)
					if (!duty.team.empty())
					{
						const auto asset = "team:" + duty.team;
						if (enabled_system_owner(a.ship, duty.team) == a && away_asset_owned(a, asset) &&
						    !away_reserved(a.ship, asset) && away_asset_healthy(a.ship, asset) &&
						    away_slot_accepts(a.ship, s, asset))
							r["choices"].push_back(asset);
					}
				if (station_stars(a) >= 3)
					for (const auto &o : definition(a.ship).personnel.officers)
					{
						const auto asset = "officer:" + o.id;
						if (away_asset_owned(a, asset) && !away_reserved(a.ship, asset) &&
						    away_slot_accepts(a.ship, s, asset))
							r["choices"].push_back(asset);
					}
			}
			row["slots"].push_back(r);
		}
		row["labels"] = Json::object();
		if (!manual_workload(a, "transport") ||
		    (station_stars(a) < 3 &&
		     !entities.at(a.ship).get<EntityControls>().layout.manual_workloads.contains(a.console)))
			row["transports"] = Json::array();
		row["labels"]["ship"] = "Own ship — docking";
		for (const auto &[craft, e] : craft_rows())
			if (e.get<CraftState>().home == a.ship)
				row["labels"]["craft:" + std::to_string(craft)] =
				    definition(craft).sensor_class + " #" + std::to_string(craft);
		for (const auto &slot : row["slots"])
		{
			const auto asset = slot.at("assigned").get<std::string>();
			if (!asset.empty())
				row["labels"][asset] = away_asset_label(a.ship, asset);
			for (const auto &choice : slot.at("choices"))
			{
				const auto key = choice.get<std::string>();
				row["labels"][key] = away_asset_label(a.ship, key);
			}
		}
		for (const auto &key : m.accompanying)
			row["labels"]["officer:" + key] = away_asset_label(a.ship, "officer:" + key);
		out.push_back(row);
	}
	return out;
}

auto Runtime::save_away() const -> Json
{
	Json out = Json::array();
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		Json row = away_operations.mission_record(i);
		for (const auto &[team, care] : away_operations.missions()[i].care)
		{
			const auto equipment = away_equipment(away_ship(away_operations.definitions()[i]), team);
			const auto *order = team_order_definition(equipment.get<CombatMechanism>(), care.order);
			if (!order)
				throw std::runtime_error("Missing away medical order");
			row["care"][team]["continuation"] = state_tree::save(order->program, care.continuation);
		}
		out.push_back(row);
	}
	return out;
}

auto Runtime::restore_away(const Json &rows) -> void
{
	away_operations.restore_order(rows);
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &d = away_operations.definitions()[i];
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
		    (definition(d.target).combat &&
		     m.entry_compartment >= static_cast<std::int64_t>(definition(d.target).combat->compartments.size())))
			throw std::runtime_error("Invalid away entry compartment");
		if (record.contains("deployed"))
			throw std::runtime_error("Inconsistent saved deployment");
		if (!m.repair_compartment.empty() && !d.repair_compartments.contains(m.repair_compartment))
			throw std::runtime_error("Invalid saved repair target");
		const auto ship = away_ship(d);
		if (m.escorts.size() > 8 || (ship && m.authorised_departments.size() > definition(ship).consoles.size()) ||
		    m.supply_quantity < 0 || m.supply_quantity > 1000000000 || (m.supply_job && !d.supply))
			throw std::runtime_error("Invalid mission contributions");
		for (const auto &console : m.authorised_departments)
			if (!ship || !seat(ship, console))
				throw std::runtime_error("Invalid contributing department");
		std::set<EntityId> escorts;
		for (const auto &[slot, id] : m.escorts)
		{
			const auto fit = std::find_if(d.craft_slots.begin(), d.craft_slots.end(),
			                              [&](const auto &s)
			                              {
				                              return s.id == slot && s.role == "escort";
			                              });
			if (fit == d.craft_slots.end() || !entities.contains(id) || !entities.at(id).has<CraftState>() ||
			    entities.at(id).get<CraftState>().home != ship || definition(id).craft->role != fit->craft_role ||
			    !escorts.insert(id).second || m.transport == "craft:" + std::to_string(id))
				throw std::runtime_error("Invalid saved mission escort");
		}

		if (m.revision < 1 || m.revision > 9007199254740991ULL || m.sequence > 9007199254740991ULL || m.progress < 0 ||
		    m.work < 0 || m.progress > m.work || m.work > 1728000000 || m.transfer < 0 ||
		    m.effects.size() > effect_receipt_limit || m.assignments.size() > 16 || m.participants.size() > 256 ||
		    (!m.ever_deployed && !m.participants.empty()))
			throw std::runtime_error("Invalid away continuation");
		for (const auto &asset : m.participants)
		{
			bool valid = asset.starts_with("team:") && away_team_duty(ship, asset.substr(5));
			if (asset.starts_with("officer:"))
				for (const auto &o : definition(ship).personnel.officers)
					valid |= asset == "officer:" + o.id;
			if (!valid)
				throw std::runtime_error("Invalid away participant");
		}
		if (!m.transport.empty() && (!ship || (!definition(ship).displacements.contains(m.transport) &&
		                                       !away_transport_entity(ship, m.transport))))
			throw std::runtime_error("Invalid saved transport");
		if ((m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound || m.deployed()) && m.transport.empty())
			throw std::runtime_error("Missing saved transport");
		if ((m.phase == AwayPhase::Recovered && !m.recovered) || m.accompanying.size() > 128 ||
		    m.checkpoint.size() > 256 || m.work_key.size() > 256 || m.outcome.size() > 512)
			throw std::runtime_error("Inconsistent away phase");
		if (!m.transport.empty() && m.transfer >= away_transfer_ticks(d, m))
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
			if (s == d.slots.end() || !away_slot_accepts(ship, *s, asset) || away_reserved(ship, asset))
				throw std::runtime_error("Invalid saved assignment");
		}
		std::set<std::string> attached;
		for (const auto &key : m.accompanying)
		{
			if (!attached.insert(key).second || assets.contains("officer:" + key) ||
			    away_reserved(ship, "officer:" + key))
				throw std::runtime_error("Duplicate accompanying officer");
			bool valid = false;
			const auto &pd = definition(ship).personnel;
			const auto &ps = entities.at(ship).get<PersonnelState>();
			for (std::size_t j = 0; j < pd.officers.size(); ++j)
				if (pd.officers[j].id == key)
					for (const auto &[slot, asset] : m.assignments)
						if (asset.starts_with("team:") &&
						    ps.officers[j].duty == away_team_duty(ship, asset.substr(5))->id)
							valid = true;
			if (!valid)
				throw std::runtime_error("Invalid accompanying officer");
		}
		if (ship)
		{
			const auto &pd = definition(ship).personnel;
			const auto &ps = entities.at(ship).get<PersonnelState>();
			for (std::size_t j = 0; j < pd.officers.size(); ++j)
				for (const auto &[slot, asset] : m.assignments)
				{
					if (asset.starts_with("team:") &&
					    ps.officers[j].duty == away_team_duty(ship, asset.substr(5))->id &&
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
			const auto equipment = away_equipment(ship, team);
			if (!assigned || !equipment)
				throw std::runtime_error("Orphaned away care");
			const auto *order = team_order_definition(equipment.get<CombatMechanism>(), care.order);
			const auto &pd = definition(d.target).personnel;
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
		validate_boarding_state(d, m);
		m.continuation = state_tree::load(d.program, rows[i].at("continuation"));
		away_operations.observe_sequence(m.sequence);
		away_operations.missions()[i] = std::move(m);
	}
}
} // namespace phoenix::world_detail
