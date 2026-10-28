module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.gameplay;
import phoenix.content.documents;
import phoenix.content.effects.types;
import phoenix.workshop.authoring;
import phoenix.workshop.tree;
import phoenix.workshop.test_client;

namespace phoenix
{
#include "state.hpp"
void GameplayEditors::State::property_value_field(Json &owner, const char *field, const Json &definition)
{
	const auto kind = definition.value("kind", std::string("quantity"));
	if (owner.contains(field) && (kind == "flag"   ? !owner.at(field).is_boolean()
	                              : kind == "enum" ? !owner.at(field).is_string()
	                                               : !owner.at(field).is_number_integer()))
	{
		text("The value type does not match the selected property kind.");
		if (button("Reset to selected property kind"))
			owner[field] = kind == "flag" ? Json(false) : kind == "enum" ? Json("") : Json(0);
		return;
	}
	if (kind == "flag")
		boolean(owner, field,
		        definition.contains("default") && definition.at("default").is_boolean()
		            ? definition.at("default").get<bool>()
		            : false);
	else if (kind == "enum")
	{
		std::vector<std::string> choices;
		if (definition.contains("choices") && definition.at("choices").is_object())
			for (const auto &[id, unused] : definition.at("choices").items())
				choices.push_back(id);
		choice(owner, field, choices);
	}
	else
		integer(owner, field,
		        definition.contains("default") && definition.at("default").is_number_integer()
		            ? definition.at("default").get<std::int64_t>()
		            : 0);
}

Json GameplayEditors::State::property_template(WorkshopDocument &document, const std::string &id)
{
	try
	{
		return document.preview_definition(WorkshopDefinitionKind::Property, id);
	}
	catch (const std::exception &)
	{
		return Json::object();
	}
}

void GameplayEditors::State::owner_processes_fields(Json &owner, WorkshopDocument &document)
{
	ScopedId scope("owner-processes");
	ScopedTree panel(label("Processes").c_str());
	if (!panel)
		return;
	if (owner.contains("processes") && owner.at("processes").is_object())
		object_rows(owner["processes"],
		            [&](const std::string &id, Json &binding)
		            {
			            ScopedId identity(id.c_str());
			            ImGui::TextUnformatted(id.c_str());
			            choice(binding, "process", document.definition_choices(WorkshopDefinitionKind::Effect));
			            boolean(binding, "requested", true);
			            optional_string(binding, "native_id");
			            return button("Remove process declaration");
		            });
	if (button("Declare process"))
	{
		if (!owner.contains("processes"))
			owner["processes"] = Json::object();
		owner["processes"][fresh(owner["processes"], "process")] = {{"process", ""}, {"requested", true}};
	}
}

void GameplayEditors::State::owner_properties_fields(Json &owner, WorkshopDocument &document)
{
	owner_processes_fields(owner, document);
	ScopedId scope("custom-properties");
	ScopedTree panel(label("Custom properties").c_str());
	if (!panel)
		return;
	if (owner.contains("properties") && owner.at("properties").is_object())
		object_rows(owner["properties"],
		            [&](const std::string &id, Json &value)
		            {
			            ScopedId identity(id.c_str());
			            const auto definition = property_template(document, id);
			            ImGui::TextUnformatted(definition.value("label", id).c_str());
			            if (value.is_object() && value.empty())
			            {
				            text("Uses the property template default.");
				            if (button("Set initial value"))
					            value = definition.value("default", Json(0));
			            }
			            else
			            {
				            property_value_field(owner["properties"], id.c_str(), definition);
				            if (button("Use property default"))
					            value = Json::object();
			            }
			            return button("Remove property declaration");
		            });
	static std::map<ImGuiID, std::string> selected;
	auto &id = selected[ImGui::GetID("new-property")];
	Json selection = {{"property", id}};
	if (choice(selection, "property", document.definition_choices(WorkshopDefinitionKind::Property)))
		id = selection.at("property").get<std::string>();
	if (button("Declare property") && !id.empty())
	{
		if (!owner.contains("properties"))
			owner["properties"] = Json::object();
		if (!owner["properties"].contains(id))
			owner["properties"][id] = Json::object();
	}
}

void GameplayEditors::State::rule_bindings_fields(Json &source, WorkshopDocument &document, bool officer)
{
	records(source, "rules", {{"id", "rule"}, {"rule", ""}, {"target", "subject"}},
	        [&](Json &binding)
	        {
		        string(binding, "id");
		        choice(binding, "rule", document.definition_choices(WorkshopDefinitionKind::Effect));
		        choice(binding, "target",
		               {"owner", "source", "instigator", "subject", "target", "attacker", "protected"});
		        optional_string(binding, "equipment");
		        optional_string(binding, "officer");
		        strings(binding, "tags");
		        if (officer)
			        choice(binding, "eligibility", {"duty", "lifetime"});
	        });
}

void GameplayEditors::State::rule_expression_field(Json &source, const char *key, Json initial)
{
	ScopedId id(key);
	ScopedTree panel(key);
	if (!panel)
		return;
	if (!source.contains(key))
	{
		if (button("Add expression"))
			source[key] = std::move(initial);
	}
	if (source.contains(key))
	{
		structure(source[key]);
		if (button("Remove expression"))
			source.erase(key);
	}
}

void GameplayEditors::State::effect_costs_fields(Json &group, const char *key, const char *quantity,
                                                 WorkshopDocument &document)
{
	records(group, key, {{"payer", "target"}, {"store", ""}, {"resource", ""}, {quantity, 0}},
	        [&](Json &cost)
	        {
		        choice(cost, "payer", {"owner", "source", "instigator", "subject", "target", "attacker", "protected"});
		        optional_string(cost, "equipment");
		        optional_string(cost, "property");
		        if (!cost.value("property", std::string{}).empty())
		        {
			        choice(cost, "property", document.definition_choices(WorkshopDefinitionKind::Property));
			        cost.erase("store");
			        cost.erase("resource");
		        }
		        else
		        {
			        string(cost, "store");
			        choice(cost, "resource", document.definition_choices(WorkshopDefinitionKind::Resource));
		        }
		        integer(cost, quantity);
	        });
}

void GameplayEditors::State::effect_operations_fields(Json &group, WorkshopDocument &document)
{
	optional_string(group, "payer");
	integer(group, "energy");
	effect_costs_fields(group, "costs", "amount", document);
	records(group, "operations", {{"kind", "fill"}, {"target", "protected"}, {"store", ""}, {"resource", ""}},
	        [&](Json &operation)
	        {
		        const auto old = operation.value("kind", std::string("fill"));
		        choice(operation, "kind",
		               {"damage", "apply", "remove", "add", "consume", "set", "fill", "transfer", "property_set",
		                "property_add", "property_subtract", "property_rate", "heat_add", "heat_remove",
		                "structural_repair", "fault_repair", "treat", "stabilise", "recover"});
		        const auto kind = operation.value("kind", std::string("fill"));
		        if (kind != old)
		        {
			        const auto target = operation.value("target", std::string("protected"));
			        operation = {{"kind", kind}, {"target", target}};
		        }
		        choice(operation, "target",
		               {"owner", "source", "instigator", "subject", "target", "attacker", "protected"});
		        if (kind == "structural_repair" || kind == "fault_repair" || kind == "treat" || kind == "stabilise" ||
		            kind == "recover")
		        {
			        if (kind == "structural_repair")
			        {
				        optional_string(operation, "compartment");
				        integer(operation, "amount", 1);
			        }
			        else if (kind == "fault_repair")
			        {
				        optional_string(operation, "equipment");
				        string(operation, "fault");
			        }
			        else
			        {
				        optional_string(operation, "officer");
				        if (kind == "treat")
					        optional_string(operation, "equipment");
			        }
			        return;
		        }
		        if (kind == "heat_add" || kind == "heat_remove")
		        {
			        optional_string(operation, "equipment");
			        optional_string(operation, "store");
			        integer(operation, "amount", 1);
			        return;
		        }
		        if (kind == "damage")
			        integer(operation, "damage", 1);
		        else if (kind == "apply" || kind == "remove")
		        {
			        if (kind == "remove")
				        choice(operation, "scope", {"local", "owner"});
			        if (kind == "remove" && operation.value("scope", std::string("local")) == "owner")
			        {
				        operation["target"] = "owner";
				        for (const auto *field :
				             {"grant", "effect", "equipment", "officer", "all_sources", "role", "delta_permille"})
					        operation.erase(field);
				        return;
			        }
			        string(operation, "grant");
			        optional_string(operation, "equipment");
			        optional_string(operation, "officer");
			        if (kind == "apply")
			        {
				        choice(operation, "effect", document.definition_choices(WorkshopDefinitionKind::Effect));
				        optional_string(operation, "role");
				        ScopedId delta_id("delta_permille");
				        if (operation.contains("delta_permille"))
				        {
					        integer(operation, "delta_permille");
					        if (button("Use template deltas"))
						        operation.erase("delta_permille");
				        }
				        else if (button("Override modifier deltas"))
					        operation["delta_permille"] = 0;
			        }
		        }
		        else if (kind.starts_with("property_"))
		        {
			        choice(operation, "property", document.definition_choices(WorkshopDefinitionKind::Property));
			        optional_string(operation, "equipment");
			        optional_string(operation, "officer");
			        boolean(operation, "saturate");
			        if (kind == "property_set")
				        property_value_field(operation, "value",
				                             property_template(document, operation.value("property", std::string{})));
			        else
				        integer(operation, "amount");
		        }
		        else
		        {
			        string(operation, "store");
			        choice(operation, "resource", document.definition_choices(WorkshopDefinitionKind::Resource));
			        if (kind != "fill")
				        integer(operation, "amount", 1);
			        if (kind == "transfer")
				        string(operation, "from_store");
		        }
	        });
}

void GameplayEditors::State::templates(WorkshopDocument &document)
{
	auto &defs = document.definitions();
	records(defs, "properties", {{"id", "property"}, {"kind", "quantity"}, {"label", "Property"}, {"default", 0}},
	        [&](Json &property)
	        {
		        string(property, "id");
		        string(property, "label");
		        choice(property, "extends", document.definition_choices(WorkshopDefinitionKind::Property), true);
		        choice(property, "kind", {"quantity", "attribute", "flag", "enum"});
		        const auto kind = property.value("kind", std::string("quantity"));
		        if (kind == "quantity" || kind == "attribute")
		        {
			        optional_string(property, "unit");
			        integer(property, "minimum");
			        integer(property, "maximum", resource_limit);
			        integer(property, "display_scale", 1);
		        }
		        if (kind == "enum")
			        scalar_map(property, "choices");
		        property_value_field(property, "default", property);
		        text("Changing property kind preserves values; verification reports incompatible fields.");
	        });
	records(defs, "resources",
	        {{"id", "resource"}, {"label", "Resource"}, {"tags", Json::array({"cargo"})}, {"sizes", {{"cargo", 1}}}},
	        [&](Json &resource)
	        {
		        string(resource, "id");
		        string(resource, "label");
		        choice(resource, "extends", document.definition_choices(WorkshopDefinitionKind::Resource), true);
		        strings(resource, "tags");
		        scalar_map(resource, "sizes");
		        names(resource, "effects", document.definition_choices(WorkshopDefinitionKind::Effect));
		        if (resource.contains("ammunition"))
		        {
			        ScopedId scope("resource-ammunition");
			        auto &ammo = resource["ammunition"];
			        integer(ammo, "fire_ticks");
			        integer(ammo, "load_ticks");
			        if (ammo.contains("projectile"))
				        projectile_fields(ammo["projectile"]);
			        else if (button("Add ammunition projectile"))
				        ammo["projectile"] = Json::object();
			        if (button("Remove ammunition profile"))
				        resource.erase("ammunition");
		        }
		        else if (button("Add ammunition profile"))
			        resource["ammunition"] = Json::object();
	        });
	records(
	    defs, "recipes",
	    {{"id", "recipe"}, {"label", "Recipe"}, {"work", 20}, {"inputs", Json::object()}, {"outputs", Json::object()}},
	    [&](Json &recipe)
	    {
		    string(recipe, "id");
		    string(recipe, "label");
		    choice(recipe, "extends", document.definition_choices(WorkshopDefinitionKind::Recipe), true);
		    strings(recipe, "tags");
		    integer(recipe, "work", 20);
		    scalar_map(recipe, "inputs");
		    scalar_map(recipe, "outputs");
	    });
	if (auto *ship = select_entity(document))
		records(*ship, "processors", {{"id", "processor"}, {"equipment", ""}, {"mode", "batch"}},
		        [&](Json &processor)
		        {
			        string(processor, "id");
			        string(processor, "equipment");
			        processor_fields(processor);
		        });
	records(
	    defs, "effects", {{"id", "effect"}},
	    [&](Json &effect)
	    {
		    string(effect, "id");
		    choice(effect, "kind",
		           {"shield", "modifier", "operations", "directed_mode", "targeting_policy", "payload", "periodic",
		            "ability", "psychic", "ability_grant", "process", "process_grant", "rule"},
		           true);
		    choice(effect, "extends", document.definition_choices(WorkshopDefinitionKind::Effect), true);
		    text("Changing template kind preserves authored fields. Verification reports fields incompatible with "
		         "the selected kind.");
		    auto kind = effect.value("kind", std::string{});
		    if (kind.empty() && effect.contains("extends"))
		    {
			    try
			    {
				    kind =
				        document
				            .preview_definition(WorkshopDefinitionKind::Effect, effect.at("extends").get<std::string>())
				            .value("kind", std::string{});
			    }
			    catch (const std::exception &)
			    {
			    }
		    }
		    if (kind == "shield" || kind == "modifier" || kind == "ability_grant" || kind == "process" ||
		        kind == "process_grant")
			    rule_bindings_fields(effect, document);
		    if (kind == "rule")
		    {
			    choice(effect, "trigger",
			           {"activate", "deactivate", "tick", "enter", "exit", "occupancy", "maintain", "pre_shield_damage",
			            "post_shield_damage", "pre_compartment_damage", "post_compartment_damage", "pre_system_damage",
			            "post_system_damage", "post_entity_damage", "destroy", "property_changed"});
			    choice(effect, "phase", {"instant", "periodic", "maintained"});
			    choice(effect, "effect", document.definition_choices(WorkshopDefinitionKind::Effect), true);
			    if (effect.value("phase", std::string("instant")) == "periodic")
				    integer(effect, "period_ticks", 1);
			    rule_expression_field(effect, "when", true);
			    rule_expression_field(effect, "damage_permille", {{"value", 1000}, {"unit", "permille"}});
			    rule_expression_field(effect, "energy", {{"value", 0}, {"unit", "energy"}});
			    rule_expression_field(effect, "values", Json::object());
			    rule_expression_field(effect, "costs", Json::array());
			    rule_expression_field(effect, "funding", {{"shortage", "pause"}, {"costs", Json::array()}});
			    return;
		    }
		    if (kind == "process")
		    {
			    string(effect, "label");
			    choice(effect, "native",
			           {"effects", "generation", "propulsion", "cooling", "processor", "repair", "medical", "recovery",
			            "servicing_energy", "servicing_ammunition", "ftl"});
			    if (effect.value("native", std::string("effects")) == "effects")
			    {
				    if (effect.contains("required_work"))
				    {
					    integer(effect, "required_work", 1);
					    if (button("Remove finite work requirement"))
						    effect.erase("required_work");
				    }
				    else if (button("Add finite work requirement"))
					    effect["required_work"] = 1;
			    }
			    boolean(effect, "requested", true);
			    optional_string(effect, "family");
			    choice(effect, "shortage", {"proportional", "pause", "stop"});
			    rule_expression_field(effect, "requirements", true);
			    for (const auto *field : {"rate", "upkeep_scale", "work_scale"})
				    rule_expression_field(effect, field, {{"value", 1000}, {"unit", "permille"}});
			    names(effect, "observations", document.definition_choices(WorkshopDefinitionKind::Property));
			    effect_costs_fields(effect, "entry_costs", "amount", document);
			    effect_costs_fields(effect, "upkeep_costs", "per_second", document);
			    effect_costs_fields(effect, "work_costs", "per_work", document);
			    for (const auto *field : {"start_effect", "pause_effect", "resume_effect", "stop_effect", "work_effect",
			                              "completion_effect"})
				    choice(effect, field, document.definition_choices(WorkshopDefinitionKind::Effect), true);
			    rule_expression_field(effect, "work_values", Json::object());
			    records(effect, "exclusions", {{"id", "exclusive"}, {"scope", "source"}},
			            [&](Json &exclusion)
			            {
				            string(exclusion, "id");
				            choice(exclusion, "scope", {"source"});
			            });
			    return;
		    }
		    if (kind == "process_grant")
		    {
			    choice(effect, "process", document.definition_choices(WorkshopDefinitionKind::Effect));
			    optional_string(effect, "native_id");
			    integer(effect, "duration_ticks", 100);
			    return;
		    }
		    if (kind == "ability")
		    {
			    audio_fields(effect, "activation_audio");
			    string(effect, "label");
			    boolean(effect, "sustained", false);
			    boolean(effect, "require_healthy", true);
			    optional_string(effect, "family");
			    rule_expression_field(effect, "requirements", true);
			    choice(effect, "requirement_failure", {"end_retain_intent"});
			    names(effect, "observations", document.definition_choices(WorkshopDefinitionKind::Property));
			    rule_expression_field(effect, "costs", Json::array());
			    choice(effect, "target", {"self", "contact", "ally"});
			    choice(effect, "recovery_on", {"activation", "stop"});
			    choice(effect, "interruption", {"cancel", "pause"});
			    choice(effect, "shortage", {"proportional", "pause", "stop"});
			    for (const auto *field :
			         {"preparation_ticks", "recovery_ticks", "energy_per_second", "preparation_energy_per_second",
			          "apparatus_recovery_ticks", "interruption_recovery_ticks"})
				    integer(effect, field);
			    optional_string(effect, "apparatus_recovery_key");
			    for (const auto *field : {"start_effect", "activation_effect", "psychic_effect"})
				    choice(effect, field, document.definition_choices(WorkshopDefinitionKind::Effect), true);
			    records(effect, "exclusions", {{"id", "exclusive"}, {"scope", "source"}},
			            [&](Json &exclusion)
			            {
				            string(exclusion, "id");
				            choice(exclusion, "scope", {"source", "apparatus"});
			            });
			    return;
		    }
		    if (kind == "psychic")
		    {
			    choice(effect, "psychic_kind", {"cloak", "scramble", "ward"});
			    optional_string(effect, "label");
			    integer(effect, "strength_permille", 700);
			    if (effect.value("psychic_kind", std::string{}) == "scramble")
			    {
				    optional_string(effect, "apparent_class");
				    for (const auto *field :
				         {"ghost_count", "offset_mm", "spacing_mm", "speed_mm_s", "acceleration_mm_s2", "turn_mdeg_s",
				          "radius_mm", "scan_ticks", "fade_ticks"})
					    integer(effect, field, effect.value(field, 1LL));
			    }
			    return;
		    }
		    if (kind == "ability_grant")
		    {
			    choice(effect, "ability", document.definition_choices(WorkshopDefinitionKind::Effect));
			    integer(effect, "duration_ticks", 100);
			    return;
		    }
		    if (kind == "targeting_policy")
		    {
			    string(effect, "label");
			    choice(effect, "policy", {"designated", "intercept", "off"});
			    return;
		    }
		    if (kind == "directed_mode")
		    {
			    string(effect, "label");
			    choice(effect, "operation", {"launch", "beam", "scan"});
			    if (effect.value("operation", std::string{}) != "scan" || effect.contains("firing_audio"))
				    audio_fields(effect, "firing_audio");
			    names(effect, "targeting", document.definition_choices(WorkshopDefinitionKind::Effect));
			    names(effect, "payloads", document.definition_choices(WorkshopDefinitionKind::Effect));
			    boolean(effect, "resource_payloads", true);
			    integer(effect, "range_mm", 1000000);
			    integer(effect, "ammo_use", 1);
			    integer(effect, "fire_ticks", 20);
			    integer(effect, "penetration", 1000);
			    integer(effect, "damage_per_m", 1000000);
			    for (const auto *field : {"energy_per_second", "discrete_energy", "auto_energy_per_second",
			                              "damage_per_second", "load_ticks", "shield_bypass_permille"})
				    integer(effect, field);
			    if (effect.value("operation", std::string{}) == "scan")
				    scalar_map(effect, "capabilities");
			    if (effect.contains("projectile"))
			    {
				    projectile_fields(effect["projectile"]);
				    if (button("Remove mode projectile"))
					    effect.erase("projectile");
			    }
			    else if (button("Add mode projectile"))
				    effect["projectile"] = Json::object();
			    return;
		    }
		    if (kind == "payload")
		    {
			    rule_bindings_fields(effect, document);
			    return;
		    }
		    if (kind == "periodic")
		    {
			    integer(effect, "duration_ticks", 100);
			    rule_bindings_fields(effect, document);
			    return;
		    }
		    if (kind == "operations")
		    {
			    effect.erase("contributions");
			    effect.erase("duration_ticks");
			    effect_operations_fields(effect, document);
			    return;
		    }
		    effect.erase("operations");
		    effect.erase("payer");
		    effect.erase("energy");
		    const bool modifier = kind == "modifier";
		    integer(effect, "duration_ticks");
		    if (button("Add contribution"))
		    {
			    auto &contributions = effect["contributions"];
			    if (!contributions.is_object())
				    contributions = Json::object();
			    std::string name = "arc";
			    unsigned index = 1;
			    while (contributions.contains(name))
				    name = "arc" + std::to_string(index++);
			    contributions[name] =
			        modifier ? Json{{"property", "recharge"}, {"delta_permille", 100}} : Json{{"capacity", 100}};
		    }
		    std::string remove;
		    if (effect.contains("contributions") && effect.at("contributions").is_object())
			    for (auto &[name, arc] : effect["contributions"].items())
			    {
				    ScopedId scope(name.c_str());
				    heading(name.c_str());
				    if (modifier)
				    {
					    std::vector<std::string> properties;
					    for (const auto &property : modifier_properties())
						    properties.push_back(property.id);
					    for (const auto &id : document.definition_choices(WorkshopDefinitionKind::Property))
					    {
						    const auto definition = property_template(document, id);
						    if (definition.value("kind", std::string{}) != "quantity")
							    properties.push_back(id);
					    }
					    choice(arc, "property", properties);
					    optional_string(arc, "role");
					    const auto definition = property_template(document, arc.value("property", std::string{}));
					    const auto type = definition.value("kind", std::string{});
					    if (type == "flag" || type == "enum")
					    {
						    arc.erase("delta_permille");
						    integer(arc, "priority");
						    property_value_field(arc, "value", definition);
					    }
					    else
					    {
						    arc.erase("priority");
						    arc.erase("value");
						    integer(arc, "delta_permille");
					    }
					    if (button("Remove contribution"))
						    remove = name;
					    continue;
				    }
				    for (const char *key :
				         {"capacity", "initial", "bearing_mdeg", "arc_mdeg", "layer", "sustain_energy_per_second",
				          "energy_per_strength", "recharge_per_second", "off_recharge_per_second", "warmup_ticks",
				          "discharge_per_second", "discharge_efficiency_permille"})
					    integer(arc, key);
				    audio_fields(arc, "audio", true);
				    scalar_map(arc, "modifiers");
				    rule_bindings_fields(arc, document);
				    if (button("Remove contribution"))
					    remove = name;
			    }
		    if (!remove.empty())
			    effect["contributions"].erase(remove);
	    });
	records(defs, "equipment",
	        {{"id", "equipment"}, {"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}},
	        [&](Json &equipment)
	        {
		        string(equipment, "id");
		        choice(equipment, "extends", document.definition_choices(WorkshopDefinitionKind::Equipment), true);
		        choice(equipment, "kind",
		               {"engine", "steering", "radar", "generator", "distribution", "coolant", "storage", "lateral",
		                "boost", "impulse", "shield", "magazine", "weapon", "turret", "team"});
		        const auto kind = equipment_kind(document, equipment);
		        system_details(equipment, kind);
		        rule_bindings_fields(equipment, document);
		        owner_properties_fields(equipment, document);
		        if (kind == "shield")
			        choice(equipment, "shield_effect", document.definition_choices(WorkshopDefinitionKind::Effect));
		        telemetry_records(equipment, document);
		        scalar_map(equipment, "parameters");
	        });
	records(
	    defs, "assemblies",
	    {{"id", "assembly"}, {"systems", Json::object()}, {"resources", Json::object()}, {"services", Json::object()}},
	    [&](Json &assembly)
	    {
		    string(assembly, "id");
		    text("Assembly internal identities receive a placement prefix. Bind service names explicitly; "
		         "$service references resolve to those bindings.");
		    static std::string system_id, service_id;
		    ImGui::InputText(label("Internal System identity").c_str(), &system_id);
		    if (button("Add internal System") && !system_id.empty())
		    {
			    assembly["systems"][system_id] = {{"template", ""}};
			    system_id.clear();
		    }
		    if (assembly.contains("systems"))
			    for (auto it = assembly["systems"].begin(); it != assembly["systems"].end(); ++it)
			    {
				    ScopedId id_scope_23(it.key().c_str());
				    ImGui::TextUnformatted(it.key().c_str());
				    choice(it.value(), "template", document.definition_choices(WorkshopDefinitionKind::Equipment),
				           true);
				    choice(it.value(), "kind",
				           {"engine", "steering", "radar", "generator", "distribution", "coolant", "storage", "lateral",
				            "boost", "impulse"},
				           true);
				    system_details(it.value(), equipment_kind(document, it.value()));
				    owner_processes_fields(it.value(), document);
				    text("Absent values inherit from the equipment template. Endpoint names and $service "
				         "bindings remain local to this assembly until placement.");
				    scalar_map(it.value(), "parameters");
				    telemetry_records(it.value(), document);
			    }
		    ImGui::InputText(label("Service name").c_str(), &service_id);
		    if (button("Add service") && !service_id.empty())
		    {
			    assembly["services"][service_id] = {{"kind", "power"}, {"direction", "input"}};
			    service_id.clear();
		    }
		    if (assembly.contains("services"))
			    object_rows(assembly["services"],
			                [&](const std::string &name, Json &row)
			                {
				                ImGui::TextUnformatted(name.c_str());
				                choice(row, "kind", {"power", "coolant", "information"});
				                choice(row, "direction", {"input", "output"});
				                string(row, "target");
				                return button("Remove service");
			                });
		    if (ScopedTree scope(label("Assembly resource network").c_str()); scope)
		    {
			    if (assembly.contains("resources"))
			    {
				    std::vector<std::string> external;
				    if (assembly.contains("services"))
					    for (const auto &service : ids(assembly.at("services")))
						    external.push_back("$" + service);
				    network_editor(assembly["resources"], external);
			    }
			    else if (button("Create assembly network"))
				    assembly["resources"] = Json::object();
		    }
	    });
}
} // namespace phoenix
