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
			            edit_registered(binding, "gameplay-editors.effects.requested");
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
	records(source, "rules", workshop_record("gameplay-editors.effects.rules"),
	        [&](Json &binding)
	        {
		        edit_registered(binding, "gameplay-editors.effects.id");
		        choice(binding, "rule", document.definition_choices(WorkshopDefinitionKind::Effect));
		        edit_registered(binding, "gameplay-editors.effects.target");
		        optional_string(binding, "equipment");
		        optional_string(binding, "officer");
		        strings(binding, "tags");
		        if (officer)
			        edit_registered(binding, "gameplay-editors.effects.eligibility");
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
		        edit_registered(cost, "gameplay-editors.effects.payer");
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
			        edit_registered(cost, "gameplay-editors.effects.store");
			        choice(cost, "resource", document.definition_choices(WorkshopDefinitionKind::Resource));
		        }
		        integer(cost, quantity);
	        });
}

void GameplayEditors::State::effect_operations_fields(Json &group, WorkshopDocument &document)
{
	optional_string(group, "payer");
	edit_registered(group, "gameplay-editors.effects.energy");
	effect_costs_fields(group, "costs", "amount", document);
	records(group, "operations", workshop_record("gameplay-editors.effects.operations"),
	        [&](Json &operation)
	        {
		        const auto old = operation.value("kind", std::string("fill"));
		        edit_registered(operation, "gameplay-editors.effects.kind");
		        const auto kind = operation.value("kind", std::string("fill"));
		        if (kind != old)
		        {
			        const auto target = operation.value("target", std::string("protected"));
			        operation = {{"kind", kind}, {"target", target}};
		        }
		        edit_registered(operation, "gameplay-editors.effects.target");
		        if (kind == "structural_repair" || kind == "fault_repair" || kind == "treat" || kind == "stabilise" ||
		            kind == "recover")
		        {
			        if (kind == "structural_repair")
			        {
				        optional_string(operation, "compartment");
				        edit_registered(operation, "gameplay-editors.effects.amount");
			        }
			        else if (kind == "fault_repair")
			        {
				        optional_string(operation, "equipment");
				        edit_registered(operation, "gameplay-editors.effects.fault");
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
			        edit_registered(operation, "gameplay-editors.effects.amount");
			        return;
		        }
		        if (kind == "damage")
			        edit_registered(operation, "gameplay-editors.effects.damage");
		        else if (kind == "apply" || kind == "remove")
		        {
			        if (kind == "remove")
				        edit_registered(operation, "gameplay-editors.effects.scope");
			        if (kind == "remove" && operation.value("scope", std::string("local")) == "owner")
			        {
				        operation["target"] = "owner";
				        for (const auto *field :
				             {"grant", "effect", "equipment", "officer", "all_sources", "role", "delta_permille"})
					        operation.erase(field);
				        return;
			        }
			        edit_registered(operation, "gameplay-editors.effects.grant");
			        optional_string(operation, "equipment");
			        optional_string(operation, "officer");
			        if (kind == "apply")
			        {
				        choice(operation, "effect", document.definition_choices(WorkshopDefinitionKind::Effect));
				        optional_string(operation, "role");
				        ScopedId delta_id("delta_permille");
				        if (operation.contains("delta_permille"))
				        {
					        edit_registered(operation, "gameplay-editors.effects.delta_permille");
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
			        edit_registered(operation, "gameplay-editors.effects.saturate");
			        if (kind == "property_set")
				        property_value_field(operation, "value",
				                             property_template(document, operation.value("property", std::string{})));
			        else
				        edit_registered(operation, "gameplay-editors.effects.amount.2");
		        }
		        else
		        {
			        edit_registered(operation, "gameplay-editors.effects.store");
			        choice(operation, "resource", document.definition_choices(WorkshopDefinitionKind::Resource));
			        if (kind != "fill")
				        edit_registered(operation, "gameplay-editors.effects.amount");
			        if (kind == "transfer")
				        edit_registered(operation, "gameplay-editors.effects.from_store");
		        }
	        });
}

void GameplayEditors::State::templates(WorkshopDocument &document)
{
	auto &defs = document.definitions();
	records(defs, "control_widgets", workshop_record("gameplay-editors.effects.control_widgets"),
	        [&](Json &widget)
	        {
		        edit_registered(widget, "gameplay-editors.effects.id");
		        edit_registered(widget, "gameplay-editors.effects.label");
		        choice(widget, "extends", document.definition_choices(WorkshopDefinitionKind::ControlWidget), true);
		        tree_editor.tree_binding(document, widget, "tree", "controller", true);
		        edit_registered(widget, "gameplay-editors.effects.policy_template");
		        if (widget.contains("policy_template"))
			        structured_field(widget, "ai");
		        text("Choose a tree or an inline policy. Inline policies can bind equipment roles in their System "
		             "observations. Repeat runs a member policy for each attached member under one Controller.");
		        if (edit_registered(widget, "gameplay-editors.effects.repeat") && widget.at("repeat") == "")
			        widget.erase("repeat");
		        structured_field(widget, widget.contains("repeat") ? "member_policy" : "policy");
		        text("Equipment roles use $role in command consumers. Instances bind fitted equipment; they cannot "
		             "replace the shared policy.");
		        structured_field(widget, "equipment");
		        structured_field(widget, "commands");
		        const auto commands = widget.value("commands", Json::array());
		        structured_field(widget, "operations");
		        auto responsibilities = ids(commands);
		        const auto operations = widget.value("operations", Json::object());
		        for (const auto &[operation, equipment] : operations.items())
			        responsibilities.push_back(operation);
		        records(widget, "levels", workshop_record("gameplay-editors.effects.levels"),
		                [&](Json &level)
		                {
			                edit_registered(level, "gameplay-editors.effects.id");
			                names(level, "human", responsibilities);
			                structured_field(level, "replaces");
			                structured_field(level, "data");
		                });
		        text("Data items declare a label, permitted binding and main/details/hidden placement. Ship instances "
		             "may override placement per star.");
		        structured_field(widget, "data");
		        strings(widget, "parts");
		        structured_field(widget, "part_bindings");
	        });
	records(defs, "properties", workshop_record("gameplay-editors.effects.properties"),
	        [&](Json &property)
	        {
		        edit_registered(property, "gameplay-editors.effects.id");
		        edit_registered(property, "gameplay-editors.effects.label");
		        choice(property, "extends", document.definition_choices(WorkshopDefinitionKind::Property), true);
		        edit_registered(property, "gameplay-editors.effects.kind.2");
		        const auto kind = property.value("kind", std::string("quantity"));
		        if (kind == "quantity" || kind == "attribute")
		        {
			        optional_string(property, "unit");
			        edit_registered(property, "gameplay-editors.effects.minimum");
			        integer(property, "maximum", resource_limit);
			        edit_registered(property, "gameplay-editors.effects.display_scale");
		        }
		        if (kind == "enum")
			        scalar_map(property, "choices");
		        property_value_field(property, "default", property);
		        text("Changing property kind preserves values; verification reports incompatible fields.");
	        });
	records(defs, "resources", workshop_record("gameplay-editors.effects.resources"),
	        [&](Json &resource)
	        {
		        edit_registered(resource, "gameplay-editors.effects.id");
		        edit_registered(resource, "gameplay-editors.effects.label");
		        choice(resource, "extends", document.definition_choices(WorkshopDefinitionKind::Resource), true);
		        strings(resource, "tags");
		        scalar_map(resource, "sizes");
		        names(resource, "effects", document.definition_choices(WorkshopDefinitionKind::Effect));
		        if (resource.contains("ammunition"))
		        {
			        ScopedId scope("resource-ammunition");
			        auto &ammo = resource["ammunition"];
			        edit_registered(ammo, "gameplay-editors.effects.fire_ticks");
			        edit_registered(ammo, "gameplay-editors.effects.load_ticks");
			        if (ammo.contains("radar_coverage"))
			        {
				        edit_registered(ammo, "gameplay-editors.effects.radar_coverage");
				        if (button("Inherit weapon coverage"))
					        ammo.erase("radar_coverage");
			        }
			        else if (button("Override radar coverage"))
				        ammo["radar_coverage"] = true;
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
	records(defs, "recipes", workshop_record("gameplay-editors.effects.recipes"),
	        [&](Json &recipe)
	        {
		        edit_registered(recipe, "gameplay-editors.effects.id");
		        edit_registered(recipe, "gameplay-editors.effects.label");
		        choice(recipe, "extends", document.definition_choices(WorkshopDefinitionKind::Recipe), true);
		        strings(recipe, "tags");
		        edit_registered(recipe, "gameplay-editors.effects.work");
		        scalar_map(recipe, "inputs");
		        scalar_map(recipe, "outputs");
	        });
	if (auto *ship = select_entity(document))
		records(*ship, "processors", workshop_record("gameplay-editors.effects.processors"),
		        [&](Json &processor)
		        {
			        edit_registered(processor, "gameplay-editors.effects.id");
			        edit_registered(processor, "gameplay-editors.effects.equipment");
			        processor_fields(processor);
		        });
	records(defs, "effects", workshop_record("gameplay-editors.effects.effects"),
	        [&](Json &effect)
	        {
		        edit_registered(effect, "gameplay-editors.effects.id");
		        edit_registered(effect, "gameplay-editors.effects.kind.3");
		        choice(effect, "extends", document.definition_choices(WorkshopDefinitionKind::Effect), true);
		        text("Changing template kind preserves authored fields. Verification reports fields incompatible with "
		             "the selected kind.");
		        auto kind = effect.value("kind", std::string{});
		        if (kind.empty() && effect.contains("extends"))
		        {
			        try
			        {
				        kind = document
				                   .preview_definition(WorkshopDefinitionKind::Effect,
				                                       effect.at("extends").get<std::string>())
				                   .value("kind", std::string{});
			        }
			        catch (const std::exception &error)
			        {
				        text("Cannot preview inherited effect.");
				        text(error.what());
			        }
		        }
		        if (kind == "shield" || kind == "modifier" || kind == "ability_grant" || kind == "process" ||
		            kind == "process_grant")
			        rule_bindings_fields(effect, document);
		        if (kind == "rule")
		        {
			        edit_registered(effect, "gameplay-editors.effects.trigger");
			        edit_registered(effect, "gameplay-editors.effects.phase");
			        choice(effect, "effect", document.definition_choices(WorkshopDefinitionKind::Effect), true);
			        if (effect.value("phase", std::string("instant")) == "periodic")
				        edit_registered(effect, "gameplay-editors.effects.period_ticks");
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
			        edit_registered(effect, "gameplay-editors.effects.label");
			        edit_registered(effect, "gameplay-editors.effects.native");
			        if (effect.value("native", std::string("effects")) == "effects")
			        {
				        if (effect.contains("required_work"))
				        {
					        edit_registered(effect, "gameplay-editors.effects.required_work");
					        if (button("Remove finite work requirement"))
						        effect.erase("required_work");
				        }
				        else if (button("Add finite work requirement"))
					        effect["required_work"] = 1;
			        }
			        edit_registered(effect, "gameplay-editors.effects.requested");
			        optional_string(effect, "family");
			        edit_registered(effect, "gameplay-editors.effects.shortage");
			        rule_expression_field(effect, "requirements", true);
			        for (const auto *field : {"rate", "upkeep_scale", "work_scale"})
				        rule_expression_field(effect, field, {{"value", 1000}, {"unit", "permille"}});
			        names(effect, "observations", document.definition_choices(WorkshopDefinitionKind::Property));
			        effect_costs_fields(effect, "entry_costs", "amount", document);
			        effect_costs_fields(effect, "upkeep_costs", "per_second", document);
			        effect_costs_fields(effect, "work_costs", "per_work", document);
			        for (const auto *field : {"start_effect", "pause_effect", "resume_effect", "stop_effect",
			                                  "work_effect", "completion_effect"})
				        choice(effect, field, document.definition_choices(WorkshopDefinitionKind::Effect), true);
			        rule_expression_field(effect, "work_values", Json::object());
			        records(effect, "exclusions", workshop_record("gameplay-editors.effects.exclusions"),
			                [&](Json &exclusion)
			                {
				                edit_registered(exclusion, "gameplay-editors.effects.id");
				                edit_registered(exclusion, "gameplay-editors.effects.scope.2");
			                });
			        return;
		        }
		        if (kind == "process_grant")
		        {
			        choice(effect, "process", document.definition_choices(WorkshopDefinitionKind::Effect));
			        optional_string(effect, "native_id");
			        edit_registered(effect, "gameplay-editors.effects.duration_ticks");
			        return;
		        }
		        if (kind == "ability")
		        {
			        audio_fields(effect, "activation_audio");
			        edit_registered(effect, "gameplay-editors.effects.label");
			        edit_registered(effect, "gameplay-editors.effects.sustained");
			        edit_registered(effect, "gameplay-editors.effects.require_healthy");
			        optional_string(effect, "family");
			        rule_expression_field(effect, "requirements", true);
			        edit_registered(effect, "gameplay-editors.effects.requirement_failure");
			        names(effect, "observations", document.definition_choices(WorkshopDefinitionKind::Property));
			        rule_expression_field(effect, "costs", Json::array());
			        edit_registered(effect, "gameplay-editors.effects.target.2");
			        edit_registered(effect, "gameplay-editors.effects.recovery_on");
			        edit_registered(effect, "gameplay-editors.effects.interruption");
			        edit_registered(effect, "gameplay-editors.effects.shortage");
			        for (const auto *field :
			             {"preparation_ticks", "recovery_ticks", "energy_per_second", "preparation_energy_per_second",
			              "apparatus_recovery_ticks", "interruption_recovery_ticks"})
				        integer(effect, field);
			        optional_string(effect, "apparatus_recovery_key");
			        for (const auto *field : {"start_effect", "activation_effect", "psychic_effect"})
				        choice(effect, field, document.definition_choices(WorkshopDefinitionKind::Effect), true);
			        records(effect, "exclusions", workshop_record("gameplay-editors.effects.exclusions"),
			                [&](Json &exclusion)
			                {
				                edit_registered(exclusion, "gameplay-editors.effects.id");
				                edit_registered(exclusion, "gameplay-editors.effects.scope.3");
			                });
			        return;
		        }
		        if (kind == "psychic")
		        {
			        edit_registered(effect, "gameplay-editors.effects.psychic_kind");
			        optional_string(effect, "label");
			        edit_registered(effect, "gameplay-editors.effects.strength_permille");
			        if (effect.value("psychic_kind", std::string{}) == "scramble")
			        {
				        optional_string(effect, "apparent_class");
				        for (const auto *field :
				             {"ghost_count", "offset_mm", "spacing_mm", "speed_mm_s", "acceleration_mm_s2",
				              "turn_mdeg_s", "radius_mm", "scan_ticks", "fade_ticks"})
					        integer(effect, field, effect.value(field, 1LL));
			        }
			        return;
		        }
		        if (kind == "ability_grant")
		        {
			        choice(effect, "ability", document.definition_choices(WorkshopDefinitionKind::Effect));
			        edit_registered(effect, "gameplay-editors.effects.duration_ticks");
			        return;
		        }
		        if (kind == "targeting_policy")
		        {
			        edit_registered(effect, "gameplay-editors.effects.label");
			        edit_registered(effect, "gameplay-editors.effects.policy");
			        return;
		        }
		        if (kind == "directed_mode")
		        {
			        edit_registered(effect, "gameplay-editors.effects.label");
			        edit_registered(effect, "gameplay-editors.effects.operation");
			        if (effect.value("operation", std::string{}) != "scan" || effect.contains("firing_audio"))
				        audio_fields(effect, "firing_audio");
			        names(effect, "targeting", document.definition_choices(WorkshopDefinitionKind::Effect));
			        names(effect, "payloads", document.definition_choices(WorkshopDefinitionKind::Effect));
			        edit_registered(effect, "gameplay-editors.effects.resource_payloads");
			        edit_registered(effect, "gameplay-editors.effects.range_mm");
			        edit_registered(effect, "gameplay-editors.effects.ammo_use");
			        edit_registered(effect, "gameplay-editors.effects.fire_ticks.2");
			        edit_registered(effect, "gameplay-editors.effects.penetration");
			        edit_registered(effect, "gameplay-editors.effects.damage_per_m");
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
			        edit_registered(effect, "gameplay-editors.effects.duration_ticks");
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
		        edit_registered(effect, "gameplay-editors.effects.duration_ticks.2");
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
						        edit_registered(arc, "gameplay-editors.effects.priority");
						        property_value_field(arc, "value", definition);
					        }
					        else
					        {
						        arc.erase("priority");
						        arc.erase("value");
						        edit_registered(arc, "gameplay-editors.effects.delta_permille");
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
	records(defs, "equipment", workshop_record("gameplay-editors.effects.equipment"),
	        [&](Json &equipment)
	        {
		        edit_registered(equipment, "gameplay-editors.effects.id");
		        choice(equipment, "extends", document.definition_choices(WorkshopDefinitionKind::Equipment), true);
		        edit_registered(equipment, "gameplay-editors.effects.kind.4");
		        const auto kind = equipment_kind(document, equipment);
		        system_details(document, equipment, kind);
		        rule_bindings_fields(equipment, document);
		        owner_properties_fields(equipment, document);
		        if (kind == "shield")
			        choice(equipment, "shield_effect", document.definition_choices(WorkshopDefinitionKind::Effect));
		        telemetry_records(equipment, document);
		        scalar_map(equipment, "parameters");
	        });
	records(defs, "assemblies", workshop_record("gameplay-editors.effects.assemblies"),
	        [&](Json &assembly)
	        {
		        edit_registered(assembly, "gameplay-editors.effects.id");
		        text("Assembly internal identities receive a placement prefix. Bind service names explicitly; "
		             "$service references resolve to those bindings.");
		        static std::string system_id, service_id;
		        tool_prepare_action("Add internal System");
		        ImGui::InputText(label("Internal System identity").c_str(), &system_id);
		        tool_action_line("Add internal System");
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
				        edit_registered(it.value(), "gameplay-editors.effects.kind.5");
				        system_details(document, it.value(), equipment_kind(document, it.value()));
				        owner_processes_fields(it.value(), document);
				        text("Absent values inherit from the equipment template. Endpoint names and $service "
				             "bindings remain local to this assembly until placement.");
				        scalar_map(it.value(), "parameters");
				        telemetry_records(it.value(), document);
			        }
		        tool_prepare_action("Add service");
		        ImGui::InputText(label("Service name").c_str(), &service_id);
		        tool_action_line("Add service");
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
				                    edit_registered(row, "gameplay-editors.effects.kind.6");
				                    edit_registered(row, "gameplay-editors.effects.direction");
				                    edit_registered(row, "gameplay-editors.effects.target.3");
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
