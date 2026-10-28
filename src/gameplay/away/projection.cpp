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
Json AwayRuntime::board(const ConsoleAddress &viewer, const AwayObservation &observation,
                        const AwayProjectionSource &permitted) const
{
	Json out = Json::array();
	for (std::size_t i = 0; i < missions().size(); ++i)
	{
		const auto &d = definitions()[i];
		const auto &m = missions()[i];
		if (origin(d, observation.slots) != viewer.ship || m.phase == AwayPhase::Hidden)
			continue;
		const auto access = permitted.permitted_row(i, viewer);
		Json row{{"id", d.id},
		         {"title", d.title},
		         {"phase", m.phase},
		         {"result", m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Lost ? m.outcome : std::string{}},
		         {"revision", m.revision},
		         {"transport", m.transport},
		         {"target", d.target},
		         {"dispatch", access.dispatch},
		         {"blocker", access.blocker},
		         {"ship_transport_reason", d.allow_ship_transport ? std::string{}
		                                   : d.ship_transport_reason.empty()
		                                       ? std::string{"Ship transport disabled by mission"}
		                                       : d.ship_transport_reason},
		         {"slots", Json::array()},
		         {"transports", access.transports},
		         {"accompanying", m.accompanying}};
		row["owner"] = access.owner;
		row["dispatch_system"] = access.dispatch_system;
		row["automatic_dispatch"] = access.automatic_dispatch;
		row["can_contribute"] = access.can_contribute;
		row["contributing"] = m.authorised_departments.contains(viewer.console);
		row["transport_owned"] = access.transport_owned;
		row["craft_slots"] = Json::array();
		std::size_t craft_slot = 0;
		for (const auto &slot : d.craft_slots)
			if (slot.role == "escort")
			{
				const auto assigned =
				    m.escorts.contains(slot.id) ? "craft:" + std::to_string(m.escorts.at(slot.id)) : std::string{};
				const auto &choices = access.craft_slots.at(craft_slot++);
				row["craft_slots"].push_back({{"id", slot.id},
				                              {"role", slot.role},
				                              {"required", slot.required},
				                              {"assigned", assigned},
				                              {"owned", choices.owned},
				                              {"lost", m.flags.contains("escort_lost." + slot.id)},
				                              {"choices", choices.choices}});
			}
		if (d.supply)
		{
			const auto &supply = access.supply.value();
			row["supply"] = {{"material", d.supply->material}, {"payment", d.supply->payment},
			                 {"give", d.supply->give},         {"pay", d.supply->pay},
			                 {"quantity", supply.quantity},    {"payment_quantity", supply.payment_quantity},
			                 {"phase", supply.phase},          {"delivered", supply.delivered},
			                 {"manual", supply.manual},        {"quantities", supply.quantities}};
		}
		if (d.boarding && m.boarding.reported)
			row["boarding_check"] = {{"chance_permille", m.boarding.chance},
			                         {"defenders", m.boarding.defenders},
			                         {"encounter", m.boarding.encounter},
			                         {"factors", m.boarding.factors}};
		for (std::size_t slot_index = 0; slot_index < d.slots.size(); ++slot_index)
		{
			const auto &slot = d.slots[slot_index];
			const auto assigned = m.assignments.contains(slot.id) ? m.assignments.at(slot.id) : std::string{};
			const auto &choices = access.slots.at(slot_index);
			row["slots"].push_back({{"id", slot.id},
			                        {"required", slot.required},
			                        {"assigned", assigned},
			                        {"owned", choices.owned},
			                        {"choices", choices.choices}});
		}
		row["labels"] = access.labels;
		out.push_back(row);
	}
	return out;
}
} // namespace phoenix
