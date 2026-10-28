module;
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

export module phoenix.world.assembly;
export import phoenix.world.state;
export import phoenix.flight;
export import phoenix.gameplay.away.state;
export import phoenix.control.controllers;
export import phoenix.effects.contracts;
export import phoenix.content.catalog;
export import phoenix.content.equipment.types;
export import phoenix.content.combat.types;
export import phoenix.content.operations.types;
export import phoenix.content.logistics.types;
export import phoenix.content.sensing.types;
export import phoenix.content.resources.types;
export import phoenix.conversations;

export namespace phoenix
{
struct CompiledGameplaySystem
{
	std::optional<FlightDefinition> flight;
};

struct CompiledGameplay
{
	std::vector<CompiledGameplaySystem> values;
	std::optional<ShipResourcesDefinition> power;
};

struct CompiledDefinition
{
	CustomPropertyState properties;
	std::map<std::string, CustomPropertyState> equipment_properties, officer_properties, compartment_properties;
	std::string faction;
	std::optional<CombatDefinition> combat;
	std::optional<DestructionDefinition> destruction;
	std::int64_t durability = 0, half_height = 5000;
	BridgeDefinition bridge;
	PersonnelDefinition personnel;
	PsychicDefinition psychic;
	AbilityDefinitionSet abilities;
	ProcessDefinitionSet processes;
	SourceRuleDefinitionSet rules;
	std::vector<BoardingDefinition> boarding;
	std::map<std::string, std::vector<std::string>> boarding_docking_ports;
	TransportDefinition transport;
	LogisticsDefinition logistics;
	std::vector<ProcessorDefinition> processors;
	std::vector<DockPortDefinition> docking;
	std::optional<CraftDefinition> craft;
	std::map<std::string, CraftBayDefinition> craft_bays;
	std::map<std::string, DisplacementDefinition> displacements;
	std::map<std::string, FtlDefinition> ftl;
	std::map<std::string, ConsoleBridgeDefinition> console_bridge;
	std::vector<std::string> tags;
	std::map<std::string, std::vector<std::string>> radar_shows;
	std::map<std::string, SensorDefinition> sensors;
	std::map<std::string, DirectedDefinition> directed;
	std::string sensor_class;
	std::vector<std::string> sprite_entries;
	std::uint32_t sprite_entry = 0;
	std::optional<ControllableLayout> layout;
	std::vector<ConsoleControllerRuntime> controller_templates;
	std::map<std::string, std::string> observation_owners;
	RadarIcon radar_icon = RadarIcon::contact;
	std::string radar_sprite;
	Collider collider;
	std::vector<ConsoleSpec> consoles;
	bool explicit_stations = false;
	std::optional<CompiledFittings> fitted;
	std::optional<CompiledGameplay> gameplay;
	std::vector<std::string> publications;
	std::vector<std::string> message_templates;
};

struct EntityAssembly
{
	CompiledDefinition definition;
	float visual_scale = 1;
	std::vector<CommsParticipant> participants;
};
EntityAssembly compile_world_entity(const Catalog &source, const Catalog &content, const std::string &name,
                                    const DirectedCatalog &, const RulesCatalog &, SharedPropertyCatalog,
                                    const SharedAbilityCatalog &, const SharedProcessCatalog &,
                                    SharedResourceCatalogue);
} // namespace phoenix
