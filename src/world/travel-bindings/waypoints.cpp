module;
#include "native_headers.hpp"

module phoenix.simulation;
import :travel_bindings;

namespace phoenix::world_detail
{
auto Runtime::publish_waypoint(EntityId ship, const ControllerChannelState &state, bool reached) -> void
{
	const auto &schema = knowledge.definition();
	const auto key = schema.schema("phoenix.waypoint");
	KnowledgeRecordInput record;
	record.schema = key;
	record.producer = schema.role("phoenix.scenario");
	record.subject = ship;
	record.provenance = {ship, ecs.get<Clock>().tick, state.revision * 2 + (reached ? 1 : 0), 0, EvidenceKind::Fact};
	record.fields = {{schema.field(key, "active"), state.value != 0},
	                 {schema.field(key, "reached"), reached},
	                 {schema.field(key, "x"), state.arguments.at(0)},
	                 {schema.field(key, "y"), state.arguments.at(1)},
	                 {schema.field(key, "revision"), static_cast<std::int64_t>(state.revision)}};
	knowledge.publish_ship(ship, record, ecs.get<Clock>().tick);
}
auto Runtime::waypoint_arrivals() -> void
{
	for (const auto &[id, entity] : entities)
		if (entity.has<EntityControls>())
			for (const auto &controller : entity.get<EntityControls>().controllers)
				for (std::size_t i = 0; i < controller.commands.size(); ++i)
				{
					const auto &state = controller.commands[i];

					if (controller.definition.commands[i].action != OperatingAction::Waypoint || !state.value)
						continue;
					const auto board = knowledge.project_json({id, controller.definition.console});
					bool reached = false;
					for (const auto &record : board.at("shared"))
						if (record.at("schema") == "phoenix.waypoint")
							reached = record.at("fields").value("reached", false);
					const auto from = step_starts.at(id), to = entity.get<Pose>().position;
					ScenarioRegion region;
					region.centre = {state.arguments[0], state.arguments[1]};
					region.radius = controller.definition.commands[i].arrival_radius;
					if (!reached && scenario_crosses(region, {from.x, from.y}, {to.x, to.y}))
						publish_waypoint(id, state, true);
				}
}

} // namespace phoenix::world_detail
