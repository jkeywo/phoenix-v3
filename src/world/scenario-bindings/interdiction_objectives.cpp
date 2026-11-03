module;
#include "native_headers.hpp"

module phoenix.simulation;
import :scenario_bindings;

namespace phoenix::world_detail
{
auto Runtime::compile_interdiction_publications() -> void
{
	if (interdiction_book.size() == 0)
		return;
	const auto &contract = knowledge.definition();
	const auto schema = contract.schema("phoenix.objective");
	interdiction_publications = {schema,
	                             contract.role("phoenix.scenario"),
	                             contract.field(schema, "id"),
	                             contract.field(schema, "behavior"),
	                             contract.field(schema, "operation"),
	                             contract.field(schema, "tier"),
	                             contract.field(schema, "scoring_policy"),
	                             contract.field(schema, "urgency"),
	                             contract.field(schema, "status"),
	                             contract.field(schema, "title"),
	                             contract.field(schema, "description"),
	                             contract.field(schema, "outcome"),
	                             contract.field(schema, "credited"),
	                             contract.field(schema, "destination_x"),
	                             contract.field(schema, "destination_y"),
	                             contract.field(schema, "cruise_speed"),
	                             contract.field(schema, "target"),
	                             contract.field(schema, "distance_limit"),
	                             contract.field(schema, "approach_distance"),
	                             contract.field(schema, "radius")};
}
auto Runtime::publish_interdiction_objectives() -> void
{
	if (interdiction_book.size() == 0)
		return;
	constexpr KnowledgeId encounter_base = 2251799813685248ULL;
	const auto now = ecs.get<Clock>().tick;
	const auto &bindings = interdiction_publications;
	for (std::size_t index = 0; index < interdiction_book.size(); ++index)
	{
		const auto &state = interdiction_book.state(index);
		const auto &definition = interdiction_book.definition(index);
		const auto source = encounter_base + index * 1024;
		const bool prize_owned = state.captured && captured_by.contains(definition.target) &&
		                         interdiction_book.participant(index, operational_owner(definition.target));
		std::vector<std::pair<EntityId, std::size_t>> actors;
		for (std::size_t slot = 0; slot < state.ships.size(); ++slot)
			actors.emplace_back(state.ships[slot], slot);
		// The prize has a fixed reserved slot, independent of fleet losses.
		if (state.captured)
			actors.emplace_back(definition.target, 63);
		for (const auto &[ship, slot] : actors)
		{
			if (!knowledge.contains_ship(ship))
				continue;
			const bool prize = slot == 63;
			const bool present = !truly_destroyed(ship) && !departed(ship) && (!prize || prize_owned);
			const bool boarding = ship == state.boarder;
			bool changed = false;
			const auto publish = [&](const char * /*kind*/, std::uint64_t ordinal, bool active,
			                         const InterdictionObjectiveMetadata &metadata, const std::string &behavior,
			                         const std::string &operation, std::map<KnowledgeKey, KnowledgeValue> parameters)
			{
				const auto id = source + slot * 16 + ordinal;
				KnowledgeRecordInput value;
				value.schema = bindings.schema;
				value.producer = bindings.producer;
				value.subject = id;
				value.withdrawn = !(present && active);
				value.fields = {{bindings.id, static_cast<std::int64_t>(id)},
				                {bindings.behavior, behavior},
				                {bindings.operation, operation},
				                {bindings.tier, metadata.tier},
				                {bindings.scoring_policy, metadata.scoring_policy},
				                {bindings.urgency, metadata.urgency},
				                {bindings.status, std::string("active")},
				                {bindings.title, metadata.title},
				                {bindings.description, metadata.description},
				                {bindings.outcome, std::string{}},
				                {bindings.credited, false}};
				value.fields.insert(parameters.begin(), parameters.end());
				KnowledgeTick revision = 1;
				bool unchanged = false;
				if (const auto previous = knowledge.latest_shared_record(ship, bindings.schema, id, source))
				{
					unchanged = previous->value.fields == value.fields && previous->value.withdrawn == value.withdrawn;
					if (!unchanged)
					{
						if (previous->value.provenance.revision == std::numeric_limits<KnowledgeTick>::max())
							throw std::runtime_error("Encounter objective revision exhausted");
						revision = previous->value.provenance.revision + 1;
					}
				}
				if (!unchanged)
				{
					value.provenance = {source, now, revision, 0, EvidenceKind::Fact};
					knowledge.publish_ship(ship, std::move(value), now);
					changed = true;
				}
			};
			const auto &flight = definition.plan_flight_objectives.contains(state.plan)
			                         ? definition.plan_flight_objectives.at(state.plan)
			                         : definition.flight_objectives;
			fixed::Vec offset;
			if (!prize && !boarding)
			{
				const auto direction =
				    fixed::direction(definition.variants.at(state.fleet_variant).at(slot).formation_bearing);
				offset = {direction.x * definition.formation / fixed::unit,
				          direction.y * definition.formation / fixed::unit};
			}
			const auto destination = [&](std::int64_t x, std::int64_t y)
			{
				const fixed::Vec point{x + offset.x, y + offset.y};
				if (point.x < -1000000000 || point.x > 1000000000 || point.y < -1000000000 || point.y > 1000000000)
					throw std::runtime_error("Authored encounter waypoint is outside coordinate limits");
				return std::map<KnowledgeKey, KnowledgeValue>{{bindings.destination_x, point.x},
				                                              {bindings.destination_y, point.y},
				                                              {bindings.cruise_speed, definition.cruise_speed}};
			};
			if (!prize)
			{
				const bool hostile = state.posture == "hostile";
				const bool working = !state.finished && !state.captured;
				const bool concealed = definition.concealed_variants.contains(state.plan);
				const auto target = static_cast<std::int64_t>(definition.target);
				const auto opponent = static_cast<std::int64_t>(slot_entities.at(definition.slot));
				const bool attack = !state.finished && hostile && (!boarding || state.captured);
				publish("hold", 1, !attack, definition.weapon_objectives.at("hold"), "hold", "hold", {});
				publish("conceal", 9, working && concealed && boarding, flight.at("formation"), "ready", "conceal", {});
				publish("attack", 2, attack, definition.weapon_objectives.at("attack"), "engage", "engage",
				        {{bindings.target, opponent},
				         {bindings.distance_limit, definition.engagement},
				         {bindings.approach_distance, definition.combat_standoff},
				         {bindings.cruise_speed, definition.cruise_speed}});
				publish("rendezvous", 3, working, flight.at("rendezvous"), "navigate", "rendezvous",
				        destination(definition.rendezvous_x, definition.rendezvous_y));
				auto boarding_parameters = destination(definition.rendezvous_x, definition.rendezvous_y);
				boarding_parameters.erase(bindings.cruise_speed);
				boarding_parameters.emplace(bindings.target, target);
				publish("board", 4, working && boarding && (hostile || state.posture == "truce"), flight.at("board"),
				        "board", "board", std::move(boarding_parameters));
				publish("formation", 5, working && !boarding && (hostile || state.posture == "truce"),
				        flight.at("formation"), "formation", "formation",
				        {{bindings.target, target},
				         {bindings.radius, definition.formation},
				         {bindings.cruise_speed, definition.cruise_speed}});
				publish("withdraw", 6, state.finished && !state.captured, flight.at("withdraw"), "withdraw", "withdraw",
				        destination(definition.departure_x, definition.departure_y));
				publish("defend", 8, working && boarding && hostile, flight.at("defend"), "engage", "defend",
				        {{bindings.target, opponent},
				         {bindings.distance_limit, definition.threat},
				         {bindings.radius, definition.clearance},
				         {bindings.approach_distance, definition.combat_standoff},
				         {bindings.cruise_speed, definition.cruise_speed}});
			}
			publish("depart", 7, state.captured || state.finished, flight.at("depart"), "depart", "depart",
			        destination(definition.departure_x, definition.departure_y));
			if (changed && entities.at(ship).has<BridgeBlackboard>())
				reconcile_objective_priority(ship);
		}
		// A declared capture cohort is an admission constraint. It issues no
		// flight, charging, docking or departure commands on behalf of members.
		if (prize_owned && !truly_destroyed(definition.target) && !departed(definition.target))
		{
			std::set<EntityId> members{definition.target};
			for (auto ship : state.ships)
				if (!truly_destroyed(ship) && !departed(ship))
					members.insert(ship);
			const auto leader = members.contains(state.boarder) ? state.boarder : definition.target;
			assign_departure_group(leader, members);
		}
	}
}

} // namespace phoenix::world_detail
