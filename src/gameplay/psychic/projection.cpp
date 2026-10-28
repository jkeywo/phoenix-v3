module;
#include <cstdint>
#include <string>
module phoenix.gameplay.psychic.projection;
import phoenix.gameplay.flight;
import phoenix.gameplay.combat.state;
namespace phoenix
{
void advance_psychic_shapes(PsychicContribution &contribution, const PsychicPowerDefinition &power)
{
	for (auto &shape : contribution.shapes)
	{
		Engine engine{power.speed, power.acceleration, power.speed, shape.acceleration_remainder};
		Steering steer{power.turn_rate, combat_angle(shape.pose.heading + power.turn_rate / simulation_tick_hz),
		               shape.turn_remainder};
		step_basic_steering(shape.pose, steer, 1000);
		step_basic_engine(shape.pose, shape.motion, engine, 1000);
		integrate_motion(shape.pose, shape.motion);
		shape.acceleration_remainder = engine.remainder;
		shape.turn_remainder = steer.remainder;
	}
}
BodyView psychic_ghost_body(const GhostReception &g, const std::string &apparent_class)
{
	BodyView b;
	b.id = g.id;
	b.definition = apparent_class;
	b.x = g.last.pose.position.x;
	b.y = g.last.pose.position.y;
	b.vx = g.last.motion.velocity.x;
	b.vy = g.last.motion.velocity.y;
	b.heading = g.last.pose.heading;
	b.altitude = g.last.pose.altitude;
	b.radar_icon = RadarIcon::ship;
	b.disposition = "hostile";
	b.faction_label = "Unknown allegiance";
	return b;
}
bool psychic_ghost_live(const GhostReception &g, std::uint64_t tick)
{
	return !g.dispelled && g.ended < 0 && g.seen == tick;
}
} // namespace phoenix
