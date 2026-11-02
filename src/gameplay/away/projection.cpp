module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>
module phoenix.gameplay.away.runtime;

namespace phoenix
{
AwayProjectionSource::~AwayProjectionSource() = default;
std::vector<AwayMissionDecision> AwayRuntime::snapshot(const ConsoleAddress &viewer, const AwayObservation &observation,
                                                       const AwayProjectionSource &permitted) const
{
	std::vector<AwayMissionDecision> out;
	for (std::size_t i = 0; i < missions().size(); ++i)
	{
		const auto &d = definitions()[i];
		const auto &m = missions()[i];
		if (origin(d, observation.slots) != viewer.ship || m.phase == AwayPhase::Hidden)
			continue;
		const auto access = permitted.permitted_row(i, viewer);
		AwayMissionDecision row;
		row.id = d.id;
		row.title = d.title;
		row.phase = away_phases.at(static_cast<std::size_t>(m.phase)).second;
		row.result = m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Lost ? m.outcome : std::string{};
		row.revision = m.revision;
		row.transport = m.transport;
		row.target = d.target;
		row.dispatch = access.dispatch;
		row.blocker = access.blocker;
		row.ship_transport_reason = d.allow_ship_transport ? std::string{}
		                            : d.ship_transport_reason.empty()
		                                ? std::string{"Ship transport disabled by mission"}
		                                : d.ship_transport_reason;
		row.transports = access.transports;
		row.accompanying = m.accompanying;
		row.owner = access.owner;
		row.ai_dispatch = d.ai_dispatch;
		row.ship_support = d.ship_support;
		row.docking_ports = d.docking_ports;
		row.work_complete = m.outcome.starts_with("complete:");
		row.dispatch_system = access.dispatch_system;
		row.automatic_dispatch = access.automatic_dispatch;
		row.can_contribute = access.can_contribute;
		row.contributing = m.authorised_departments.contains(viewer.console);
		row.transport_owned = access.transport_owned;
		std::size_t craft_slot = 0;
		for (const auto &slot : d.craft_slots)
			if (slot.role == "escort")
			{
				const auto assigned =
				    m.escorts.contains(slot.id) ? "craft:" + std::to_string(m.escorts.at(slot.id)) : std::string{};
				const auto &choices = access.craft_slots.at(craft_slot++);
				row.craft_slots.push_back({slot.id, slot.role, assigned, slot.required, choices.owned,
				                           m.flags.contains("escort_lost." + slot.id), true, choices.choices});
			}
		if (d.supply)
		{
			const auto &supply = access.supply.value();
			row.supply = AwaySupplyDecision{
			    d.supply->material, d.supply->payment,       supply.phase,     d.supply->give, d.supply->pay,
			    supply.quantity,    supply.payment_quantity, supply.delivered, supply.manual,  supply.quantities};
		}
		if (d.boarding && m.boarding.reported)
			row.boarding_check =
			    AwayBoardingDecision{m.boarding.chance, m.boarding.defenders, m.boarding.encounter, m.boarding.factors};
		for (std::size_t slot_index = 0; slot_index < d.slots.size(); ++slot_index)
		{
			const auto &slot = d.slots[slot_index];
			const auto assigned = m.assignments.contains(slot.id) ? m.assignments.at(slot.id) : std::string{};
			const auto &choices = access.slots.at(slot_index);
			row.slots.push_back({slot.id, {}, assigned, slot.required, choices.owned, false, false, choices.choices});
		}
		row.labels = access.labels;
		row.portraits = access.portraits;
		row.crew_names = access.crew_names;
		row.launch_ready = access.launch_ready;
		row.launch_blocker = access.launch_blocker;
		out.push_back(row);
	}
	return out;
}
Json AwayRuntime::board(const ConsoleAddress &viewer, const AwayObservation &observation,
                        const AwayProjectionSource &permitted) const
{
	return Json(snapshot(viewer, observation, permitted));
}
} // namespace phoenix
