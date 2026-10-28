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

export module phoenix.content.effects;
export import phoenix.data;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.controllers.types;
export import phoenix.content.effects.types;
export import phoenix.content.operations.types;
export import phoenix.content.sensing.types;

export namespace phoenix
{
std::string choir_ability_control(const std::string &apparatus, const std::string &duty);
SharedPropertyCatalog compile_property_catalog(const Catalog &);

std::map<std::string, CustomPropertyValue> compile_property_bindings(const PropertyCatalog &, const Json &,
                                                                     const std::string &);
void verify_property_bindings(const Catalog &, SharedPropertyCatalog = {});
RuleExpression compile_rule_expression(const Catalog &, RuleDefinition &, const Json &, const std::string &,
                                       SharedPropertyCatalog = {});
RuleDefinition compile_rule(const Catalog &, const std::string &, SharedPropertyCatalog compiled = {});
RulesCatalog compile_rule_catalog(const Catalog &, SharedPropertyCatalog = {});
RulesCatalog compile_bound_rules(const Catalog &, const Json &, SharedPropertyCatalog = {});
RulesCatalog compile_entity_rule_scope(const Catalog &, const std::string &, SharedPropertyCatalog = {});
std::vector<RuleBinding> compile_rule_bindings(const RulesCatalog &, const Json &, const std::string &,
                                               std::span<const RuleTrigger> allowed = {});
SourceRuleDefinitionSet compile_source_rules(const Catalog &, const std::string &, const RulesCatalog &);
SourceRuleDefinitionSet compile_source_rules(const Catalog &, const std::string &);
std::map<std::string, std::vector<RuleBinding>> compile_effect_rules(const Catalog &, const RulesCatalog &);
std::map<std::string, std::vector<RuleBinding>> compile_region_rules(const Catalog &, const Json &,
                                                                     const RulesCatalog &);

ProcessDefinition compile_process(const Catalog &, const std::string &, SharedPropertyCatalog = {});
SharedProcessCatalog compile_process_catalog(const Catalog &, SharedPropertyCatalog = {});
ProcessGrantDefinition compile_process_grant(const Catalog &, const std::string &);
ProcessDefinitionSet compile_process_bindings(const Catalog &, const std::string &, SharedProcessCatalog,
                                              SharedPropertyCatalog = {});
ProcessDefinitionSet compile_process_bindings(const Catalog &, const std::string &, SharedProcessCatalog,
                                              const ControllableLayout &, SharedPropertyCatalog = {});
std::vector<RuleCostDefinition> compile_rule_costs(const Catalog &, RuleDefinition &, const Json &, const std::string &,
                                                   const std::string &, SharedPropertyCatalog = {});
void compile_rule_values(const Catalog &, RuleDefinition &, const Json &, const std::string &,
                         SharedPropertyCatalog = {});
AbilityDefinition compile_ability(const Catalog &, const std::string &, SharedPropertyCatalog = {});
SharedAbilityCatalog compile_ability_catalog(const Catalog &, SharedPropertyCatalog = {});
AbilityGrantDefinition compile_ability_grant(const Catalog &, const std::string &);
AbilityDefinitionSet compile_abilities(const Catalog &, const std::string &);
AbilityDefinitionSet compile_abilities(const Catalog &, const std::string &, const DirectedCatalog &,
                                       SharedPropertyCatalog = {});
AbilityDefinitionSet compile_abilities(const Catalog &, const std::string &, SharedAbilityCatalog,
                                       const PersonnelDefinition &, const ControllableLayout &,
                                       const PsychicDefinition &);
std::vector<EffectCostDefinition> compile_effect_costs(const Catalog &, const Json &, const std::string &,
                                                       const std::string &quantity = "amount",
                                                       SharedPropertyCatalog = {});
EffectGroupDefinition compile_effect_operations(const Catalog &, const Json &, const std::string &,
                                                SharedPropertyCatalog = {});
EffectGroupDefinition compile_effect_group(const Catalog &, const std::string &, SharedPropertyCatalog = {});
} // namespace phoenix
