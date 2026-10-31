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
	const auto craft_reports = craft_board(a);
	const auto reported_available = [&](EntityId id)
	{
		return std::any_of(craft_reports.begin(), craft_reports.end(),
		                   [&](const auto &report)
		                   {
			                   return report.at("id").template get<std::string>() == std::to_string(id) &&
			                          (report.value("available", false) ||
			                           (m.phase == AwayPhase::Deployed && m.transport_released &&
			                            report.value("recovery_available", false)));
		                   });
	};
	AwayPermittedRow row;
	row.dispatch = away_dispatch(a, d.dispatch);
	row.blocker = away_blocker(i);
	row.owner = enabled_system_owner(a.ship, away_dispatch_system(a.ship, d.dispatch)).console;
	row.dispatch_system = away_dispatch_system(a.ship, d.dispatch);
	const auto *send_owner = native_operation_controller({a.ship, row.owner}, "mission_send", row.dispatch_system);
	row.launch_blocker = away_launch_blocker(i);
	row.launch_ready = row.dispatch && send_owner && controller_operation_human(*send_owner, "mission_send") &&
	                   row.launch_blocker.empty();
	row.automatic_dispatch =
	    !d.supply && d.allow_ship_transport && (send_owner && !controller_operation_human(*send_owner, "mission_send"));
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
	                      away_asset_owned(a, "transport:" + m.transport) && manual_workload(a, "transport");
	for (const auto &slot : d.craft_slots)
		if (slot.role == "escort")
		{
			const auto assigned =
			    m.escorts.contains(slot.id) ? "craft:" + std::to_string(m.escorts.at(slot.id)) : std::string{};
			AwayPermittedSlot r;
			r.owned = !assigned.empty() && away_asset_owned(a, assigned) && manual_workload(a, "transport");
			if (row.can_contribute)
				for (const auto &[id, e] : craft_rows())
					if (e.get<CraftState>().home == a.ship && definition(id).craft->role == slot.craft_role &&
					    away_asset_owned(a, "craft:" + std::to_string(id)) && reported_available(id))
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
		row.supply = AwayPermittedSupply{job        ? job->amount
		                                 : manifest ? manifest->amount
		                                            : 0,
		                                 job        ? job->payment_amount
		                                 : manifest ? manifest->payment_amount
		                                            : 0,
		                                 job ? job->report : "preparation",
		                                 job ? job->delivered : 0,
		                                 manual_workload(a, "mission_quantity", row.dispatch_system) &&
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
	if (m.outcome.empty() && d.allow_ship_transport && away_ship_transport_owned(a))
		row.transports.push_back("ship");
	for (const auto &[craft, e] : craft_rows())
		if (e.get<CraftState>().home == a.ship &&
		    (definition(craft).craft->role == "shuttle" || definition(craft).craft->role == "boarding_torpedo") &&
		    away_asset_owned(a, "transport:craft:" + std::to_string(craft)) && reported_available(craft) &&
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
		r.owned = !assigned.empty() && away_asset_owned(a, assigned) &&
		          (assigned.starts_with("officer:") ? manual_workload(a, "mission_staffing")
		                                            : manual_workload(a, "team_selection", assigned.substr(5)) ||
		                                                  manual_workload(a, "transport", assigned.substr(5)));
		if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
		{
			for (const auto &duty : definition(a.ship).personnel.duties)
				if (!duty.team.empty())
				{
					const auto asset = "team:" + duty.team;
					if (enabled_system_owner(a.ship, duty.team) == a && away_asset_owned(a, asset) &&
					    !away_reserved(a.ship, asset) && away_asset_healthy(a.ship, asset) &&
					    away_slot_accepts(a.ship, s, asset))
						r.choices.push_back(asset);
				}
			if (native_operation_controller(a, "mission_staffing"))
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

	const auto &crew = definition(a.ship).personnel;
	const auto &state = entities.at(a.ship).get<PersonnelState>();
	for (const auto &[asset, label] : row.labels)
	{
		std::string officer;
		if (asset.starts_with("officer:"))
			officer = asset.substr(8);
		else if (asset.starts_with("team:"))
		{
			for (const auto &duty : crew.duties)
				if (duty.team == asset.substr(5))
					for (std::size_t j = 0; j < state.officers.size(); ++j)
						if (state.officers[j].duty == duty.id)
							officer = crew.officers[j].id;
		}
		for (const auto &person : crew.officers)
			if (person.id == officer)
			{
				row.portraits[asset] = person.portrait;
				row.crew_names[asset] = person.name;
			}
	}
	return row;
}

auto Runtime::away_board(const ConsoleAddress &a) const -> Json
{
	NativeAwayReadBinding binding(*this);
	NativeAwayProjectionSource source(*this);
	auto rows = away_operations.board(a, binding.observation, source);
	for (auto &row : rows)
	{
		row["care"] = Json::array();
		for (std::size_t i = 0; i < away_operations.definitions().size(); ++i)
			if (away_operations.definitions()[i].id == row.at("id").get<std::string>())
			{
				const auto &mission = away_operations.missions()[i];
				auto local = mission;
				std::erase_if(local.assignments,
				              [&](const auto &assignment)
				              {
					              return !away_asset_owned(a, assignment.second);
				              });
				std::erase_if(local.accompanying,
				              [&](const auto &officer)
				              {
					              return !away_asset_owned(a, "officer:" + officer);
				              });
				std::int64_t assigned = local.assignments.size() + local.accompanying.size();
				for (const auto &[slot, craft] : mission.escorts)
					assigned += away_asset_owned(a, "craft:" + std::to_string(craft));
				if (!mission.transport.empty() && away_asset_owned(a, "transport:" + mission.transport))
					++assigned;
				const bool committed = mission.phase == AwayPhase::Outbound || mission.phase == AwayPhase::Deployed ||
				                       mission.phase == AwayPhase::Inbound;
				const bool staging = mission.phase == AwayPhase::Open || mission.phase == AwayPhase::Recovered;
				const bool authorised = mission.authorised_departments.contains(a.console);
				const bool ready = authorised && assigned > 0 &&
				                   (committed || (staging && !mission.transport.empty() && away_staged(a.ship, local)));
				row["department"] = {{"assigned", assigned},
				                     {"ready", ready},
				                     {"contributing", authorised},
				                     {"observed_tick", ecs.get<Clock>().tick},
				                     {"revision", mission.revision}};
				row["department"]["transport_released"] = mission.transport_released;
				row["department"]["recall_requested"] = ftl_recall_started(a.ship);
				row["department"]["launch_ready"] = row.at("dispatch").get<bool>() && away_launch_blocker(i).empty();
				row["department"]["peers"] = Json::array();
				for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
					if (controller.definition.console != a.console &&
					    controller.definition.operations.contains("contribution") &&
					    !controller.definition.operations.at("contribution").empty())
						row["department"]["peers"].push_back(
						    {{"console", controller.definition.console},
						     {"controller", controller.definition.id},
						     {"id", knowledge.controller_id(
						                {a.ship, controller.definition.console, controller.definition.id})}});
				row["department"]["transport_systems"] = Json::object();
				for (const auto &choice : row.at("transports"))
				{
					const auto selection = choice.get<std::string>();
					const auto physical = away_transport_entity(a.ship, selection);
					row["department"]["transport_systems"][selection] =
					    physical == a.ship ? row.at("dispatch_system").get<std::string>()
					    : physical         ? entities.at(physical).get<CraftState>().bay
					                       : selection;
				}
			}

		for (std::size_t i = 0; i < away_operations.definitions().size(); ++i)
		{
			const auto &definition_mission = away_operations.definitions()[i];
			const auto &mission = away_operations.missions()[i];
			if (row.at("id") != definition_mission.id || mission.phase != AwayPhase::Deployed)
				continue;
			for (const auto &[team, care] : mission.care)
			{
				const auto *controller = native_operation_controller(a, "medical", team);
				if (!controller)
					continue;
				Json report{{"team", team},
				            {"controller", controller->definition.id},
				            {"location", care.location},
				            {"destination", care.destination},
				            {"travel", care.travel},
				            {"local_complete", care.travel == 0},
				            {"observed_tick", ecs.get<Clock>().tick},
				            {"compartments", definition(definition_mission.target).personnel.compartments},
				            {"patients", Json::array()}};
				if (!care.travel)
					for (const auto &patient : clinical_patients(definition_mission.target))
						if (patient.state.location == care.location)
							report["patients"].push_back({{"id", patient.id},
							                              {"condition", patient.state.condition},
							                              {"stabilised", patient.state.stabilised}});
				row["care"].push_back(std::move(report));
			}
		}
	}
	return rows;
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
