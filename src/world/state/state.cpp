module;
#include <nlohmann/json.hpp>

module phoenix.world.state;

namespace phoenix::fixed
{
void to_json(Json &value, const Vec &vector)
{
	value["x"] = vector.x;
	value["y"] = vector.y;
}
void from_json(const Json &value, Vec &vector)
{
	value.at("x").get_to(vector.x);
	value.at("y").get_to(vector.y);
}
} // namespace phoenix::fixed

namespace phoenix
{
void to_json(Json &value, const VerticalMotion &motion)
{
	value["altitude"] = motion.altitude;
	value["velocity"] = motion.velocity;
	value["remainder"] = motion.remainder;
	value["acceleration_remainder"] = motion.acceleration_remainder;
	value["desired"] = motion.desired;
}
void from_json(const Json &value, VerticalMotion &motion)
{
	value.at("altitude").get_to(motion.altitude);
	value.at("velocity").get_to(motion.velocity);
	value.at("remainder").get_to(motion.remainder);
	value.at("acceleration_remainder").get_to(motion.acceleration_remainder);
	value.at("desired").get_to(motion.desired);
}
void to_json(Json &value, const SavedBody &body)
{
	value["id"] = body.id;
	value["definition"] = body.definition;
	value["position"] = body.position;
	value["velocity"] = body.velocity;
	value["remainder"] = body.remainder;
	value["heading"] = body.heading;
}
void from_json(const Json &value, SavedBody &body)
{
	value.at("id").get_to(body.id);
	value.at("definition").get_to(body.definition);
	value.at("position").get_to(body.position);
	value.at("velocity").get_to(body.velocity);
	value.at("remainder").get_to(body.remainder);
	value.at("heading").get_to(body.heading);
}
void to_json(Json &value, const SavedOperation &operation)
{
	value["speed"] = operation.speed;
	value["heading"] = operation.heading;
	value["acceleration_remainder"] = operation.acceleration_remainder;
	value["damping_remainder"] = operation.damping_remainder;
	value["turn_remainder"] = operation.turn_remainder;
	value["engine_enabled"] = operation.engine_enabled;
	value["radar_enabled"] = operation.radar_enabled;
}
void from_json(const Json &value, SavedOperation &operation)
{
	value.at("speed").get_to(operation.speed);
	value.at("heading").get_to(operation.heading);
	value.at("acceleration_remainder").get_to(operation.acceleration_remainder);
	value.at("damping_remainder").get_to(operation.damping_remainder);
	value.at("turn_remainder").get_to(operation.turn_remainder);
	value.at("engine_enabled").get_to(operation.engine_enabled);
	value.at("radar_enabled").get_to(operation.radar_enabled);
}
} // namespace phoenix
