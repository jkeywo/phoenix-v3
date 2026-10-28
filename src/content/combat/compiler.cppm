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

export module phoenix.content.combat;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.effects.types;

export namespace phoenix
{
ModifierEffectDefinition compile_modifier_effect(const Catalog &, const std::string &, SharedPropertyCatalog = {});
void compile_effect(const Catalog &, const std::string &, SharedPropertyCatalog = {});
void compile_effect(const Catalog &, const std::string &, const RulesCatalog &, SharedPropertyCatalog = {});
PayloadDefinition compile_payload(const Catalog &, const std::string &);
PeriodicEffectDefinition compile_periodic_effect(const Catalog &, const std::string &);
PayloadDefinition compile_payload(const Catalog &, const std::string &, const RulesCatalog &);
PeriodicEffectDefinition compile_periodic_effect(const Catalog &, const std::string &, const RulesCatalog &);
std::vector<std::string> compile_payloads(const Catalog &, const Json &, const std::string &);
ProjectileDefinition compile_projectile_profile(const Catalog &, const Json &, const std::string &);
AmmunitionDefinition compile_ammunition_resource(const Catalog &, const std::string &);
bool property_compatible(std::string_view, OperatingSystem, const DirectedDefinition &);
DirectedModeDefinition compile_directed_mode(const Catalog &, const std::string &);
DirectedTargetingDefinition compile_targeting_policy(const Catalog &, const std::string &);
std::map<std::string, DirectedDefinition> compile_directed(const Catalog &, const std::string &);
DirectedCatalog compile_directed_catalog(const Catalog &);
ShieldEffectDefinition compile_shield_effect(const Catalog &, const std::string &, SharedPropertyCatalog = {});
ShieldEffectDefinition compile_shield_effect(const Catalog &, const std::string &, const RulesCatalog &);
std::optional<CombatDefinition> compile_combat(const Catalog &, const std::string &entity, SharedPropertyCatalog = {});
std::optional<CombatDefinition> compile_combat(const Catalog &, const std::string &entity, const DirectedCatalog &,
                                               SharedPropertyCatalog = {});
std::optional<CombatDefinition> compile_combat(const Catalog &, const std::string &, const DirectedCatalog &,
                                               const RulesCatalog &);
DestructionDefinition compile_destruction(const Json &, const Catalog &, const std::string &path);
} // namespace phoenix
