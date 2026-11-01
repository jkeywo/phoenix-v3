module;
#include "native_headers.hpp"

module phoenix.simulation;
import :scenario_bindings;

namespace phoenix::world_detail
{
auto Runtime::publish_interdiction_objectives() -> void
{
	if (interdiction_book.size() == 0)
		return;
	// Scenario identities occupy the range below this reservation; standing
	// directives start at 2^52. Encounter slots and authored fleet slots are stable.
	constexpr KnowledgeId encounter_base = 2251799813685248ULL;
	const auto now = ecs.get<Clock>().tick;
	const auto &contract = knowledge.definition();
	const auto schema = contract.schema("phoenix.objective");
	for (std::size_t index = 0; index < interdiction_book.size(); ++index)
	{
		const auto &state = interdiction_book.state(index);
		const auto &definition = interdiction_book.definition(index);
		const auto source = encounter_base + index * 1024;
		for (std::size_t slot = 0; slot < state.ships.size(); ++slot)
		{
			const auto ship = state.ships[slot];
			if (!knowledge.contains_ship(ship))
				continue;
			const bool present = !state.finished && !truly_destroyed(ship) && !departed(ship);
			// These are authored scenario roles and circumstances, not targeting
			// evidence. Controllers must still establish their own feasible work.
			const bool attack =
			    state.posture == "hostile" && (ship != state.boarder || state.blocked || state.captured);
			const auto board = knowledge.public_board(ship);
			bool changed = false;
			for (const auto *kind : {"hold", "attack"})
			{
				const bool hold = std::string(kind) == "hold";
				const auto &metadata = definition.weapon_objectives.at(kind);
				const auto id = source + slot * 16 + (hold ? 1 : 2);
				KnowledgeRecordInput value;
				value.schema = schema;
				value.producer = contract.role("phoenix.scenario");
				value.subject = id;
				value.withdrawn = !(present && (hold ? !attack : attack));
				const std::map<std::string, KnowledgeValue> fields{{"id", static_cast<std::int64_t>(id)},
				                                                   {"behavior", std::string(hold ? "hold" : "engage")},
				                                                   {"operation", std::string(hold ? "hold" : "engage")},
				                                                   {"tier", metadata.tier},
				                                                   {"scoring_policy", metadata.scoring_policy},
				                                                   {"urgency", metadata.urgency},
				                                                   {"status", std::string("active")},
				                                                   {"title", metadata.title},
				                                                   {"description", metadata.description},
				                                                   {"outcome", std::string{}},
				                                                   {"credited", false}};
				Json expected = Json::object();
				for (const auto &[name, field] : fields)
				{
					value.fields[contract.field(schema, name)] = field;
					std::visit(
					    [&](const auto &datum)
					    {
						    expected[name] = datum;
					    },
					    field);
				}
				if (!hold)
				{
					const auto target = slot_entities.at(definition.slot);
					value.fields[contract.field(schema, "target")] = static_cast<std::int64_t>(target);
					expected["target"] = target;
				}
				KnowledgeTick revision = 1;
				bool unchanged = false;
				for (auto row = board.at("shared").rbegin(); row != board.at("shared").rend(); ++row)
					if (row->at("schema") == "phoenix.objective" && row->at("subject") == id)
					{
						const auto previous = row->at("revision").get<KnowledgeTick>();
						if (previous == std::numeric_limits<KnowledgeTick>::max())
							throw std::runtime_error("Encounter objective revision exhausted");
						revision = previous + 1;
						unchanged = row->at("source") == source && row->at("fields") == expected &&
						            row->at("withdrawn") == value.withdrawn;
						break;
					}
				if (!unchanged)
				{
					value.provenance = {source, now, revision, 0, EvidenceKind::Fact};
					knowledge.publish_ship(ship, std::move(value), now);
					changed = true;
				}
			}
			if (changed && entities.at(ship).has<BridgeBlackboard>())
				reconcile_objective_priority(ship);
		}
	}
}
} // namespace phoenix::world_detail
