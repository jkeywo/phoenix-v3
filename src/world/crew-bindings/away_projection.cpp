module;
#include "native_headers.hpp"

module phoenix.simulation;
import :crew_bindings;
import :away_adapters;

namespace phoenix::world_detail
{
// Control and physical-choice collection remains readonly native composition.
// The compiled owner checks disclosure before entering this permitted-row source.
auto Runtime::permitted_away_row(std::size_t i, const ConsoleAddress &a) const -> AwayPermittedRow
{
	const auto &d = away_operations.definitions()[i];
	const auto &m = away_operations.missions()[i];
	AwayPermittedRow row;
	const auto &controls = entities.at(a.ship).get<EntityControls>();
	const bool widgets =
	    std::any_of(controls.controllers.begin(), controls.controllers.end(),
	                [&](const auto &controller)
	                {
		                return controller.definition.console == a.console && !controller.definition.widget.empty();
	                });
	row.dispatch = away_dispatch(a, d.dispatch);
	row.blocker = away_blocker(i);
	row.owner = enabled_system_owner(a.ship, away_dispatch_system(a.ship, d.dispatch)).console;
	row.dispatch_system = away_dispatch_system(a.ship, d.dispatch);
	const auto *send_owner = native_operation_controller({a.ship, row.owner}, "mission_send", row.dispatch_system);
	row.automatic_dispatch = !d.supply && d.allow_ship_transport &&
	                         (send_owner ? !controller_operation_human(*send_owner, "mission_send")
	                                     : station_stars({a.ship, row.owner}) == 1);
	bool owns_assets = false;
	for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
		if (controller.definition.console == a.console)
			for (const auto &ch : controller.definition.commands)
				for (const auto consumer : ch.consumers)
					owns_assets |= definition(a.ship).fitted->values.at(consumer).kind == OperatingSystem::Team ||
					               definition(a.ship).fitted->values.at(consumer).kind == OperatingSystem::ShuttleBay;
	row.can_contribute =
	    owns_assets && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered) && m.outcome.empty();
	row.transport_owned = !m.transport.empty() && row.can_contribute &&
	                      away_asset_owned(a, "transport:" + m.transport) && manual_workload(a, "transport") &&
	                      (widgets || station_stars(a) >= 3 || controls.layout.manual_workloads.contains(a.console));
	for (const auto &slot : d.craft_slots)
		if (slot.role == "escort")
		{
			const auto assigned =
			    m.escorts.contains(slot.id) ? "craft:" + std::to_string(m.escorts.at(slot.id)) : std::string{};
			AwayPermittedSlot r;
			r.owned = !assigned.empty() && away_asset_owned(a, assigned) && manual_workload(a, "transport");
			if (row.can_contribute && manual_workload(a, "transport"))
				for (const auto &[id, e] : craft_rows())
					if (e.get<CraftState>().home == a.ship && definition(id).craft->role == slot.craft_role &&
					    away_asset_owned(a, "craft:" + std::to_string(id)) && craft_available(id, d.id))
						r.choices.push_back("craft:" + std::to_string(id));
			row.craft_slots.push_back(r);
		}
	if (d.supply)
	{
		const auto manifest = !m.supply_job ? supply_manifest(i) : std::optional<CargoDelivery>{};
		const CargoDelivery *job = nullptr;
		for (const auto &candidate : cargo_deliveries)
			if (candidate.id == m.supply_job)
				job = &candidate;
		row.supply = AwayPermittedSupply{
		    job        ? job->amount
		    : manifest ? manifest->amount
		               : 0,
		    job        ? job->payment_amount
		    : manifest ? manifest->payment_amount
		               : 0,
		    job ? job->report : "preparation",
		    job ? job->delivered : 0,
		    (widgets ? manual_workload(a, "mission_quantity", row.dispatch_system) : station_stars(a) >= 3) &&
		        away_dispatch(a, d.dispatch) && !m.supply_job,
		    {0}};
		if (manifest && row.supply->manual)
			for (const auto q :
			     {std::max(d.supply->give, manifest->amount - d.supply->give), manifest->amount + d.supply->give})
				if (const auto chosen = supply_manifest(i, q); chosen && chosen->amount == q)
					row.supply->quantities.push_back(q);
		if (!m.supply_job && !manifest && !m.transport.empty())
			row.blocker = "No compatible supply manifest: check stock, payment, capacity and docking";
	}
	if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
	{
		if (!m.transport.empty() && !away_staged(a.ship, m))
			row.blocker = "Personnel moving to transport";
		for (const auto &slot : d.slots)
			if (slot.required && !m.assignments.contains(slot.id))
				row.blocker = "Mandatory assignments required";
	}
	for (const auto &[key, t] : definition(a.ship).displacements)
		if ((m.outcome.empty() || m.phase == AwayPhase::Deployed) && away_asset_owned(a, "transport:" + key))
			row.transports.push_back(key);
	for (const auto &[key, bay] : definition(a.ship).craft_bays)
		if (bay.legacy_transport && away_asset_owned(a, "transport:" + key) &&
		    canonical_away_transport(a.ship, key, d.id) != key)
			row.transports.push_back(key);
	if (m.outcome.empty() && d.allow_ship_transport && away_ship_transport_owned(a))
		row.transports.push_back("ship");
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
			row.transports.push_back("craft:" + std::to_string(craft));
	for (const auto &s : d.slots)
	{
		const auto assigned = m.assignments.contains(s.id) ? m.assignments.at(s.id) : std::string{};
		AwayPermittedSlot r;
		r.owned =
		    !assigned.empty() && away_asset_owned(a, assigned) &&
		    (widgets ? assigned.starts_with("officer:") ? manual_workload(a, "mission_staffing")
		                                                : manual_workload(a, "team_selection", assigned.substr(5)) ||
		                                                      manual_workload(a, "transport", assigned.substr(5))
		             : station_stars(a) >= 3 || !assigned.starts_with("officer:"));
		if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
		{
			for (const auto &duty : definition(a.ship).personnel.duties)
				if (!duty.team.empty())
				{
					const auto asset = "team:" + duty.team;
					if ((!widgets || manual_workload(a, "team_selection", duty.team) ||
					     manual_workload(a, "transport", duty.team)) &&
					    enabled_system_owner(a.ship, duty.team) == a && away_asset_owned(a, asset) &&
					    !away_reserved(a.ship, asset) && away_asset_healthy(a.ship, asset) &&
					    away_slot_accepts(a.ship, s, asset))
						r.choices.push_back(asset);
				}
			if (widgets ? manual_workload(a, "mission_staffing") : station_stars(a) >= 3)
				for (const auto &o : definition(a.ship).personnel.officers)
				{
					const auto asset = "officer:" + o.id;
					if (away_asset_owned(a, asset) && !away_reserved(a.ship, asset) &&
					    away_slot_accepts(a.ship, s, asset))
						r.choices.push_back(asset);
				}
		}
		row.slots.push_back(r);
	}
	if (!manual_workload(a, "transport") ||
	    (!widgets && station_stars(a) < 3 && !controls.layout.manual_workloads.contains(a.console)))
		row.transports.clear();
	row.labels["ship"] = "Own ship — docking";
	for (const auto &[craft, e] : craft_rows())
		if (e.get<CraftState>().home == a.ship)
			row.labels["craft:" + std::to_string(craft)] =
			    definition(craft).sensor_class + " #" + std::to_string(craft);
	for (std::size_t slot_index = 0; slot_index < d.slots.size(); ++slot_index)
	{
		const auto &slot = d.slots[slot_index];
		const auto assigned = m.assignments.contains(slot.id) ? m.assignments.at(slot.id) : std::string{};
		if (!assigned.empty())
			row.labels[assigned] = away_asset_label(a.ship, assigned);
		for (const auto &choice : row.slots[slot_index].choices)
			row.labels[choice] = away_asset_label(a.ship, choice);
	}
	for (const auto &key : m.accompanying)
		row.labels["officer:" + key] = away_asset_label(a.ship, "officer:" + key);
	return row;
}

auto Runtime::away_board(const ConsoleAddress &a) const -> Json
{
	NativeAwayReadBinding binding(*this);
	NativeAwayProjectionSource source(*this);
	return away_operations.board(a, binding.observation, source);
}

auto Runtime::save_away() const -> Json
{
	NativeAwayReadBinding binding(*this);
	return away_operations.save(binding.observation);
}

auto Runtime::restore_away(const Json &rows) -> void
{
	NativeAwayPersistenceBinding binding(*this);
	away_operations.restore(rows, binding.context);
}
} // namespace phoenix::world_detail
