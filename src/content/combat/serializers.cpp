module;
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.content.combat.types;

namespace phoenix
{
void to_json(Json &value, const DestructionDefinition &state)
{
	value["visual"] = state.visual;
	value["sound"] = state.sound;
	value["chance"] = state.chance;
	value["damage"] = state.damage;
	value["radius"] = state.radius;
	value["shield_bypass"] = state.shield_bypass;
	value["growth_ticks"] = state.growth_ticks;
	value["fade_ticks"] = state.fade_ticks;
	value["size"] = state.size;
	value["affects_missiles"] = state.affects_missiles;
	value["payloads"] = state.payloads;
}
void from_json(const Json &value, DestructionDefinition &state)
{
	const DestructionDefinition defaults{};
	state.visual = value.value("visual", defaults.visual);
	state.sound = value.value("sound", defaults.sound);
	state.chance = value.value("chance", defaults.chance);
	state.damage = value.value("damage", defaults.damage);
	state.radius = value.value("radius", defaults.radius);
	state.shield_bypass = value.value("shield_bypass", defaults.shield_bypass);
	state.growth_ticks = value.value("growth_ticks", defaults.growth_ticks);
	state.fade_ticks = value.value("fade_ticks", defaults.fade_ticks);
	state.size = value.value("size", defaults.size);
	state.affects_missiles = value.value("affects_missiles", defaults.affects_missiles);
	state.payloads = value.value("payloads", defaults.payloads);
}
void to_json(Json &value, const ProjectileDefinition &state)
{
	value["visual"] = state.visual;
	value["speed"] = state.speed;
	value["lifetime"] = state.lifetime;
	value["penetration"] = state.penetration;
	value["damage"] = state.damage;
	value["deposition"] = state.deposition;
	value["spread"] = state.spread;
	value["guidance"] = state.guidance;
	value["trail"] = state.trail;
	value["shield_bypass"] = state.shield_bypass;
	value["turn_rate"] = state.turn_rate;
	value["seeker_range"] = state.seeker_range;
	value["arc_height"] = state.arc_height;
	value["chaos"] = state.chaos;
	value["tracer"] = state.tracer;
	value["durability"] = state.durability;
	value["radius"] = state.radius;
	value["resistance"] = state.resistance;
	value["blast_radius"] = state.blast_radius;
	value["fuse_radius"] = state.fuse_radius;
	value["destruction"] = state.destruction;
	value["payloads"] = state.payloads;
}
void from_json(const Json &value, ProjectileDefinition &state)
{
	const ProjectileDefinition defaults{};
	state.visual = value.value("visual", defaults.visual);
	state.speed = value.value("speed", defaults.speed);
	state.lifetime = value.value("lifetime", defaults.lifetime);
	state.penetration = value.value("penetration", defaults.penetration);
	state.damage = value.value("damage", defaults.damage);
	state.deposition = value.value("deposition", defaults.deposition);
	state.spread = value.value("spread", defaults.spread);
	state.guidance = value.value("guidance", defaults.guidance);
	state.trail = value.value("trail", defaults.trail);
	state.shield_bypass = value.value("shield_bypass", defaults.shield_bypass);
	state.turn_rate = value.value("turn_rate", defaults.turn_rate);
	state.seeker_range = value.value("seeker_range", defaults.seeker_range);
	state.arc_height = value.value("arc_height", defaults.arc_height);
	state.chaos = value.value("chaos", defaults.chaos);
	state.tracer = value.value("tracer", defaults.tracer);
	state.durability = value.value("durability", defaults.durability);
	state.radius = value.value("radius", defaults.radius);
	state.resistance = value.value("resistance", defaults.resistance);
	state.blast_radius = value.value("blast_radius", defaults.blast_radius);
	state.fuse_radius = value.value("fuse_radius", defaults.fuse_radius);
	state.destruction = value.value("destruction", defaults.destruction);
	state.payloads = value.value("payloads", defaults.payloads);
}
} // namespace phoenix
