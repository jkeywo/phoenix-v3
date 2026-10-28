module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.combat.types;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.resources;
export import phoenix.content.effects.types;
export import phoenix.content.operations.types;

export namespace phoenix
{
// Optional compiler scopes must describe the same resolved Catalog; standalone helpers compile their own scope.
struct DirectedDefinition;
// A caller-owned immutable compilation scope, shared by native fitting adapters.
using DirectedCatalog = std::map<std::string, std::map<std::string, DirectedDefinition>>;
// Combat definitions are compiled once. Runtime components never interpret JSON.
struct CompartmentDefinition
{
	std::string id, deck;
	std::int64_t x = 0, y = 0, half_width = 0, half_length = 0, maximum = 0, initial = 0, resistance = 0, recovery = 0;
	std::int64_t deck_weight = 1;
};
struct CompartmentConnection
{
	std::size_t from = 0, to = 0;
	std::int64_t ticks = 1;
};
struct DestructionDefinition
{
	std::string visual, sound;
	std::int64_t chance = 1000, damage = 0, radius = 0, shield_bypass = 0;
	std::int64_t growth_ticks = 1, fade_ticks = 10, size = 10000;
	bool affects_missiles = true;
	std::vector<std::string> payloads;
};
struct ProjectileDefinition
{
	std::string visual;
	std::int64_t speed = 0, lifetime = 0, penetration = 0, damage = 0, deposition = 0, spread = 0;
	std::string guidance = "none", trail;
	bool tracer = false;
	std::int64_t shield_bypass = 0, turn_rate = 0, seeker_range = 0, arc_height = 0, chaos = 0;
	std::int64_t durability = 0, radius = 100, resistance = 1, blast_radius = 0, fuse_radius = 0;
	DestructionDefinition destruction;
	std::vector<std::string> payloads;
};
// Continuation encoding belongs to the definition owner. Missing fields retain
// the historical WITH_DEFAULT semantics, independently of the destination value.
void to_json(Json &, const DestructionDefinition &);
void from_json(const Json &, DestructionDefinition &);
void to_json(Json &, const ProjectileDefinition &);
void from_json(const Json &, ProjectileDefinition &);
struct FaultDefinition
{
	std::string id, symptom, diagnosis, effect = "output";
	std::int64_t chance = 0, threshold = 1, severity = 1, effectiveness = 500, repair_work = 100;
	bool destroys = false;
};
struct AmmunitionDefinition
{
	std::int64_t interval = 1, load_ticks = 0;
	ProjectileDefinition projectile;
};
enum class DirectedOperationKind
{
	Launch,
	Beam,
	Scan
};
enum class DirectedTargetingKind
{
	Designated,
	Intercept,
	Off
};
struct DirectedTargetingDefinition
{
	std::string id, label;
	DirectedTargetingKind kind = DirectedTargetingKind::Designated;
};
struct DirectedModeDefinition
{
	AudioClip firing_audio;
	std::string id, label;
	DirectedOperationKind operation = DirectedOperationKind::Launch;
	std::vector<std::string> targeting;
	std::int64_t range = 0, energy_per_second = 0, discrete_energy = 0, standby_energy = 0;
	std::int64_t ammo_use = 1, beam_damage = 0;
	AmmunitionDefinition ammunition;
	std::map<std::string, std::int64_t> capabilities;
	std::vector<std::string> payloads;
	bool resource_payloads = true;
};
struct DirectedDefinition
{
	std::vector<DirectedModeDefinition> modes;
	std::vector<DirectedTargetingDefinition> targeting;
	std::size_t initial_mode = 0, initial_targeting = 0;
};
struct ReserveDefinition
{
	std::int64_t capacity = 0, initial = 0;
};
struct WeaponDefinition
{
	AmmunitionDefinition ammunition;
	std::string turret, muzzle_marker, family = "projectile";
	std::int64_t ammo_use = 1, range = 0, beam_damage = 0, standby_energy = 0;
	bool point_defence = false;
	std::vector<std::string> magazines, intercept_classes;
};
struct TurretDefinition
{
	std::string marker, visual;
	std::int64_t traverse = 0, scale_permille = 1000;
	bool flip_y = false;
};
struct MagazineDefinition
{
	StockStoreDefinition stock;
	std::map<std::string, AmmunitionDefinition> ammunition_profiles;
	std::optional<ProcessorDefinition> processor;
	AmmunitionDefinition ammunition;
	ReserveDefinition reserve;
	std::string reload = "none";
	std::int64_t refill_ticks = 1, refill_amount = 0, reload_energy = 0;
};
struct TeamDefinition
{
	bool local_repair_priority = false;
	std::int64_t repair_rate = 0, stack_penalty = 500, treatment_ticks = 400, stabilise_ticks = 200,
	             injured_effectiveness = 500;
	std::string type = "engineering";
	std::set<std::string> capabilities{"repair"};
	std::vector<TeamOrderDefinition> orders;
};
struct ShieldDefinition
{
	AudioClip audio;
	float audio_peak_gain = .8f, audio_decay = 1.5f;
	std::int64_t audio_threshold = 1, audio_full_damage = 30;
	std::vector<RuleBinding> rules;
	std::int64_t recharge_modifier = 0, sustain_modifier = 0, discharge_modifier = 0, protection_modifier = 0;
	ReserveDefinition reserve;
	std::int64_t sustain_energy = 0, recharge_energy = 1, recharge_rate = 0, off_recharge_rate = 0;
	std::int64_t warmup_ticks = 1, layer = 0, discharge_rate = 0, discharge_efficiency = 1000;
};
struct ShieldContributionDefinition
{
	std::string name;
	std::int64_t bearing = 0, arc = 360000;
	ShieldDefinition shield;
};
struct ShieldEffectDefinition
{
	std::int64_t duration_ticks = 0;
	std::vector<ShieldContributionDefinition> contributions;
};
struct CombatSystemDefinition
{
	std::string shield_effect;
	ShieldEffectDefinition shield_definition;
	std::string id, compartment;
	OperatingSystem kind = OperatingSystem::Weapon;
	std::int64_t x = 0, y = 0, bearing = 0, arc = 360000;
	std::variant<WeaponDefinition, TurretDefinition, MagazineDefinition, TeamDefinition, ShieldDefinition> details;
	explicit CombatSystemDefinition(OperatingSystem type = OperatingSystem::Weapon) : kind(type)
	{
		switch (type)
		{
		case OperatingSystem::Weapon:
			details = WeaponDefinition{};
			break;
		case OperatingSystem::Turret:
			details = TurretDefinition{};
			break;
		case OperatingSystem::Magazine:
			details = MagazineDefinition{};
			break;
		case OperatingSystem::Team:
			details = TeamDefinition{};
			break;
		case OperatingSystem::Shield:
			details = ShieldDefinition{};
			break;
		default:
			break;
		}
	}
	template <class T> T &get()
	{
		return std::get<T>(details);
	}
	template <class T> const T &get() const
	{
		return std::get<T>(details);
	}
	AmmunitionDefinition &ammunition()
	{
		return kind == OperatingSystem::Magazine ? get<MagazineDefinition>().ammunition
		                                         : get<WeaponDefinition>().ammunition;
	}
	const AmmunitionDefinition &ammunition() const
	{
		return kind == OperatingSystem::Magazine ? get<MagazineDefinition>().ammunition
		                                         : get<WeaponDefinition>().ammunition;
	}
	ReserveDefinition &reserve()
	{
		return kind == OperatingSystem::Shield ? get<ShieldDefinition>().reserve : get<MagazineDefinition>().reserve;
	}
	const ReserveDefinition &reserve() const
	{
		return kind == OperatingSystem::Shield ? get<ShieldDefinition>().reserve : get<MagazineDefinition>().reserve;
	}
};
struct CombatDefinition
{
	std::vector<CompartmentDefinition> compartments;
	std::vector<CompartmentConnection> connections;
	std::vector<CombatSystemDefinition> systems;
	std::map<std::string, std::size_t> compartment_ids, system_ids;
	std::map<std::string, std::string> equipment_compartments;
	std::map<std::string, std::vector<FaultDefinition>> faults;
	std::string repair_console, access_controller;
	std::int64_t collision_scale = 1, collision_threshold = 1000, half_height = 5000;
	DestructionDefinition destruction;
};
} // namespace phoenix
