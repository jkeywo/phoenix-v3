module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content.effects;
import phoenix.content.catalog;
import phoenix.content.combat;
import phoenix.content.combat.types;
import phoenix.content.controllers;
import phoenix.content.controllers.types;
import phoenix.content.effects.types;
import phoenix.content.operations;
import phoenix.content.operations.types;
import phoenix.content.resources;
import phoenix.content.resources.types;
import phoenix.content.sensing;
import phoenix.content.sensing.types;

namespace phoenix
{
namespace
{
void ability_fail(const std::string &path, const std::string &why)
{
	throw std::runtime_error(path + ": " + why);
}
void ability_fields(const Json &value, const std::set<std::string> &fields, const std::string &path)
{
	if (!value.is_object())
		ability_fail(path, "expected object");
	for (const auto &[key, unused] : value.items())
		if (!fields.contains(key))
			ability_fail(path + "." + key, "unsupported field");
}
std::int64_t ability_number(const Json &value, const char *field, std::int64_t initial, std::int64_t maximum,
                            const std::string &path)
{
	const auto number = value.value(field, Json(initial));
	if (!number.is_number_integer() || number < 0 || number > maximum)
		ability_fail(path + "." + field, "expected bounded nonnegative integer");
	return number.get<std::int64_t>();
}
const Json &ability_template(const Catalog &catalog, const std::string &id, const std::string &kind)
{
	const auto found = catalog.effects.find(id);
	if (found == catalog.effects.end() || found->second.value("kind", std::string{}) != kind)
		ability_fail("effects." + id, "expected " + kind + " template");
	return found->second;
}
std::vector<std::string> ability_references(const Catalog &catalog, const Json &value, const std::string &path)
{
	if (!value.is_array() || value.size() > 32)
		ability_fail(path, "expected at most 32 ability references");
	std::vector<std::string> result;
	std::set<std::string> seen;
	for (const auto &entry : value)
	{
		if (!entry.is_string() || !valid_identifier(entry.get<std::string>()) ||
		    !seen.insert(entry.get<std::string>()).second)
			ability_fail(path, "invalid or duplicate ability reference");
		const auto id = entry.get<std::string>();
		(void)ability_template(catalog, id, "ability");
		result.push_back(id);
	}
	return result;
}
} // namespace

AbilityDefinition compile_ability_definition(const Catalog &catalog, const std::string &id,
                                             const std::map<std::string, PsychicPowerDefinition> *profiles,
                                             SharedPropertyCatalog property_catalogue)
{
	const auto &value = ability_template(catalog, id, "ability");
	const auto path = "effects." + id;
	ability_fields(value,
	               {"id",
	                "kind",
	                "name",
	                "label",
	                "extends",
	                "sustained",
	                "target",
	                "preparation_ticks",
	                "recovery_ticks",
	                "recovery_on",
	                "interruption",
	                "shortage",
	                "require_healthy",
	                "energy_per_second",
	                "preparation_energy_per_second",
	                "start_effect",
	                "activation_effect",
	                "psychic_effect",
	                "exclusions",
	                "apparatus_recovery_key",
	                "apparatus_recovery_ticks",
	                "interruption_recovery_ticks",
	                "requirements",
	                "requirement_failure",
	                "family",
	                "costs",
	                "observations"},
	               path);
	AbilityDefinition result;
	result.id = id;
	result.label = value.value("label", std::string{});
	if (result.label.empty() || result.label.size() > 128)
		ability_fail(path + ".label", "expected 1..128 characters");
	for (const auto *field : {"sustained", "require_healthy"})
		if (value.contains(field) && !value.at(field).is_boolean())
			ability_fail(path + "." + field, "expected boolean");
	result.sustained = value.value("sustained", false);
	result.require_healthy = value.value("require_healthy", true);
	result.family = value.value("family", std::string{});
	const auto observed = value.value("observations", Json::array());
	if (!observed.is_array() || observed.size() > 256)
		ability_fail(path + ".observations", "expected at most 256 property references");
	std::set<std::string> observed_ids;
	for (const auto &reference : observed)
	{
		if (!reference.is_string() || !property_catalogue->contains(reference.get<std::string>()) ||
		    !observed_ids.insert(reference.get<std::string>()).second)
			ability_fail(path + ".observations", "unknown or duplicate property");
		result.observations.push_back(reference.get<std::string>());
	}

	if (!result.family.empty() && !valid_identifier(result.family))
		ability_fail(path + ".family", "expected identifier");
	result.requirement_failure = value.value("requirement_failure", std::string("end_retain_intent"));
	if (result.requirement_failure != "end_retain_intent")
		ability_fail(path + ".requirement_failure", "unsupported requirement failure policy");
	if (value.contains("requirements"))
	{
		RuleDefinition requirements;
		requirements.id = id;
		requirements.condition = compile_rule_expression(catalog, requirements, value.at("requirements"),
		                                                 path + ".requirements", property_catalogue);
		if (requirements.condition->type != state_tree::Type::Boolean)
			ability_fail(path + ".requirements", "expected Boolean expression");
		result.requirements = std::move(requirements);
	}

	result.target = value.value("target", std::string("self"));
	result.recovery_on = value.value("recovery_on", result.sustained ? std::string("stop") : std::string("activation"));
	result.interruption = value.value("interruption", std::string("cancel"));
	result.shortage = value.value("shortage", std::string("pause"));
	if ((result.target != "self" && result.target != "contact" && result.target != "ally") ||
	    (result.recovery_on != "activation" && result.recovery_on != "stop") ||
	    (result.interruption != "cancel" && result.interruption != "pause") ||
	    (result.shortage != "proportional" && result.shortage != "pause" && result.shortage != "stop"))
		ability_fail(path, "unknown target, recovery, interruption or shortage policy");
	result.preparation_ticks = ability_number(value, "preparation_ticks", 0, 1000000, path);
	result.recovery_ticks = ability_number(value, "recovery_ticks", 0, 1000000, path);
	result.interruption_recovery_ticks = ability_number(value, "interruption_recovery_ticks", 0, 1000000, path);
	result.apparatus_recovery_key = value.value("apparatus_recovery_key", std::string{});
	result.apparatus_recovery_ticks = ability_number(value, "apparatus_recovery_ticks", 0, 1000000, path);
	if ((!result.apparatus_recovery_key.empty() && !valid_identifier(result.apparatus_recovery_key)) ||
	    (result.apparatus_recovery_ticks && result.apparatus_recovery_key.empty()))
		ability_fail(path + ".apparatus_recovery_key", "apparatus recovery requires a valid explicit key");
	result.energy_per_second = ability_number(value, "energy_per_second", 0, 1000000000, path);
	result.preparation_energy_per_second = ability_number(value, "preparation_energy_per_second", 0, 1000000000, path);
	if (result.preparation_energy_per_second && !result.preparation_ticks)
		ability_fail(path + ".preparation_energy_per_second", "preparation funding requires preparation work");
	if (result.energy_per_second && !result.sustained)
		ability_fail(path + ".energy_per_second", "instant ability costs belong to start/activation effects");
	const auto effect = [&](const char *field) -> std::optional<EffectGroupDefinition>
	{
		if (!value.contains(field))
			return std::nullopt;
		if (!value.at(field).is_string())
			ability_fail(path + "." + field, "expected operations-effect reference");
		return compile_effect_group(catalog, value.at(field).get<std::string>(), property_catalogue);
	};
	result.start_effect = effect("start_effect");
	result.activation_effect = effect("activation_effect");
	if (value.contains("costs"))
	{
		if (!result.sustained && !result.preparation_ticks)
			ability_fail(path + ".costs", "ongoing costs require preparation or sustained work");
		result.costs =
		    compile_effect_costs(catalog, value.at("costs"), path + ".costs", "per_second", property_catalogue);
	}
	if (!result.preparation_ticks && result.start_effect && result.activation_effect)
	{
		const auto &start = *result.start_effect;
		const auto &activation = *result.activation_effect;
		if (start.operations.size() + activation.operations.size() > 32 ||
		    start.energy + activation.energy > 1000000000)
			ability_fail(path, "instant start and activation exceed one required group's operation/energy bounds");
		if (start.energy && activation.energy && start.payer != activation.payer)
			ability_fail(path, "instant start and activation entry-energy costs must share one payer");
	}
	result.psychic_effect = value.value("psychic_effect", std::string{});
	if (!result.psychic_effect.empty())
	{
		std::optional<PsychicPowerDefinition> standalone;
		if (!profiles)
			standalone = compile_psychic_effect(catalog, result.psychic_effect);
		else if (!profiles->contains(result.psychic_effect))
			ability_fail(path + ".psychic_effect", "unknown native psychic profile");
		const auto &profile = profiles ? profiles->at(result.psychic_effect) : *standalone;
		if (!result.sustained || (profile.kind == "cloak" && result.target != "self") ||
		    (profile.kind == "ward" && result.target != "ally") ||
		    (profile.kind == "scramble" && result.target != "contact"))
			ability_fail(path + ".psychic_effect", "native psychic operation requires its sustained target policy");
	}
	if (!result.psychic_effect.empty() && result.shortage != "proportional")
		ability_fail(path + ".shortage", "psychic contribution requires proportional native funding");
	const auto exclusions = value.value("exclusions", Json::array());
	if (!exclusions.is_array() || exclusions.size() > 16)
		ability_fail(path + ".exclusions", "expected at most 16 exclusions");
	std::set<std::pair<std::string, std::string>> seen;
	for (const auto &entry : exclusions)
	{
		ability_fields(entry, {"id", "scope"}, path + ".exclusions");
		AbilityExclusionDefinition exclusion{entry.value("id", std::string{}),
		                                     entry.value("scope", std::string("source"))};
		if (!valid_identifier(exclusion.id) || (exclusion.scope != "source" && exclusion.scope != "apparatus") ||
		    !seen.emplace(exclusion.scope, exclusion.id).second)
			ability_fail(path + ".exclusions", "invalid or duplicate exclusion");
		result.exclusions.push_back(std::move(exclusion));
	}
	return result;
}

AbilityDefinition compile_ability(const Catalog &catalog, const std::string &id, SharedPropertyCatalog properties)
{
	return compile_ability_definition(catalog, id, nullptr,
	                                  properties ? std::move(properties) : compile_property_catalog(catalog));
}

SharedAbilityCatalog compile_ability_catalog(const Catalog &catalog, SharedPropertyCatalog compiled_properties)
{
	const auto properties = compiled_properties ? std::move(compiled_properties) : compile_property_catalog(catalog);
	auto compiled = std::make_shared<AbilityCatalog>();
	for (const auto &[id, value] : catalog.effects)
		if (value.value("kind", std::string{}) == "psychic")
			compiled->psychic_profiles.emplace(id, compile_psychic_effect(catalog, id));
	for (const auto &[id, value] : catalog.effects)
		if (value.value("kind", std::string{}) == "ability")
			compiled->definitions.emplace(
			    id, compile_ability_definition(catalog, id, &compiled->psychic_profiles, properties));
	if (compiled->definitions.size() > 1024)
		ability_fail("abilities", "at most 1024 shared abilities are supported");
	return compiled;
}

std::string choir_ability_control(const std::string &apparatus, const std::string &duty)
{
	return "choir." + apparatus + "." + duty;
}

AbilityGrantDefinition compile_ability_grant(const Catalog &catalog, const std::string &id)
{
	const auto &value = ability_template(catalog, id, "ability_grant");
	const auto path = "effects." + id;
	ability_fields(value, {"id", "kind", "name", "label", "extends", "ability", "duration_ticks", "rules"}, path);
	AbilityGrantDefinition result;
	result.ability = value.value("ability", std::string{});
	(void)ability_template(catalog, result.ability, "ability");
	result.duration_ticks = ability_number(value, "duration_ticks", 0, 1000000, path);
	if (!result.duration_ticks)
		ability_fail(path + ".duration_ticks", "temporary ability grants require positive duration");
	return result;
}

AbilityDefinitionSet compile_abilities(const Catalog &catalog, const std::string &entity)
{
	const DirectedCatalog directed_catalog{{entity, compile_directed(catalog, entity)}};
	return compile_abilities(catalog, entity, directed_catalog);
}
AbilityDefinitionSet compile_abilities(const Catalog &catalog, const std::string &entity,
                                       const DirectedCatalog &directed_catalog, SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	const auto shared = compile_ability_catalog(catalog, properties);
	if (catalog.presentation)
	{
		AbilityDefinitionSet result;
		result.catalog = shared;
		return result;
	}
	const auto personnel = compile_personnel(catalog, entity, directed_catalog, properties);
	const auto layout = compile_controllable_layout(catalog, entity, properties);
	const auto psychic = compile_psychic(catalog, entity, shared, personnel, layout);
	return compile_abilities(catalog, entity, shared, personnel, layout, psychic);
}
AbilityDefinitionSet compile_abilities(const Catalog &catalog, const std::string &entity, SharedAbilityCatalog shared,
                                       const PersonnelDefinition &personnel, const ControllableLayout &layout,
                                       const PsychicDefinition &psychic)
{
	AbilityDefinitionSet result;
	result.catalog = std::move(shared);
	if (!result.catalog || result.catalog != psychic.catalog)
		ability_fail(entity + ".abilities", "native adapters require the same compiled ability catalogue");
	if (catalog.presentation)
		return result;
	const auto &hull = catalog.entities.at(entity);
	const auto process_controls = hull.value("process_controls", Json::object());
	if (!process_controls.is_object())
		ability_fail(entity + ".process_controls", "expected object");
	const auto &definitions = result.catalog->definitions;
	for (const auto &[id, system] : hull.at("systems").items())
		if (system.contains("abilities"))
			result.equipment_grants.emplace(
			    id, ability_references(catalog, system.at("abilities"), entity + ".systems." + id + ".abilities"));
	std::size_t static_grants = 0;
	const auto count_grants = [&](std::size_t count)
	{
		if (count > ability_grant_limit - static_grants)
			ability_fail(entity + ".abilities", "combined officer/equipment grants exceed the supported grant limit");
		static_grants += count;
	};
	for (const auto &officer : personnel.officers)
		count_grants(officer.abilities.size());
	for (const auto &[equipment, grants] : result.equipment_grants)
		count_grants(grants.size());
	std::set<std::pair<std::size_t, std::size_t>> channels;
	std::set<std::string> control_ids;
	bool resources_resolved = false;
	std::optional<ShipResourcesDefinition> resources;
	const auto require_power = [&](const std::string &equipment, const std::string &path)
	{
		if (!resources_resolved)
		{
			resources = compile_ship_resources(catalog, entity);
			resources_resolved = true;
		}
		if (equipment.empty() || !resources)
			ability_fail(path, "paid ability requires equipment with a native power-store binding");
		const auto binding = std::find_if(resources->systems.begin(), resources->systems.end(),
		                                  [&](const auto &system)
		                                  {
			                                  return system.id == equipment;
		                                  });
		if (binding == resources->systems.end() || binding->store == no_resource)
			ability_fail(path, "paid ability requires equipment with a native power-store binding");
	};
	const auto add = [&](AbilityControlDefinition binding, const std::string &path)
	{
		if (!valid_identifier(binding.id) || !control_ids.insert(binding.id).second)
			ability_fail(path, "invalid or duplicate ability endpoint");
		if (binding.controller >= layout.controllers.size())
			ability_fail(path, "unknown Controller binding");
		const auto &controller = layout.controllers[binding.controller];
		const auto channel = [&](std::size_t index, const char *key, OperatingAction action, bool optional)
		{
			if (optional && index == ability_no_channel)
				return;
			if (index >= controller.commands.size() || controller.commands[index].action != action ||
			    controller.commands[index].minimum < 0 || controller.commands[index].initial != 0)
				ability_fail(path + "." + key, "requires zero-initialised nonnegative intent channel");
			if (!channels.emplace(binding.controller, index).second)
				ability_fail(path + "." + key, "ability channel has multiple physical consumers");
			for (const auto &[id, process] : process_controls.items())
				if (process.value("controller", std::string{}) == controller.id &&
				    process.value("enabled", std::string{}) == controller.commands[index].id)
					ability_fail(path, "channel cannot drive both an ability and a process");
		};
		const auto ability = definitions.find(binding.ability);
		// Fixed sustained endpoints may use an explicit start/cancel request as
		// well as retained settings. Dynamic choir selection keeps its setting.
		const bool request_endpoint = ability != definitions.end() &&
		                              (!ability->second.sustained || (binding.enabled < controller.commands.size() &&
		                                                              controller.commands[binding.enabled].action ==
		                                                                  OperatingAction::AbilityActivate));
		const auto activation = request_endpoint ? OperatingAction::AbilityActivate : OperatingAction::IntentBoolean;
		channel(binding.enabled, "enabled", activation, false);
		channel(binding.target, "target", OperatingAction::IntentInteger, true);
		if (binding.target != ability_no_channel && controller.commands[binding.target].maximum > 1000000000)
			ability_fail(path + ".target", "target channel exceeds physical entity identity bounds");
		const PersonnelDuty *duty = nullptr;
		const OfficerDefinition *officer = nullptr;
		if (binding.source == "duty")
		{
			const auto found = personnel.duty_indices.find(binding.owner);
			if (found == personnel.duty_indices.end())
				ability_fail(path + ".owner", "unknown duty");
			duty = &personnel.duties.at(found->second);
		}
		else if (binding.source == "officer")
		{
			const auto found = std::find_if(personnel.officers.begin(), personnel.officers.end(),
			                                [&](const auto &o)
			                                {
				                                return o.id == binding.owner;
			                                });
			if (found == personnel.officers.end())
				ability_fail(path + ".owner", "unknown officer");
			officer = &*found;
		}
		else if (binding.source != "equipment" || !hull.at("systems").contains(binding.owner))
			ability_fail(path + ".source", "source must name a duty, officer or fitted equipment");
		if (!binding.apparatus.empty() && !hull.at("systems").contains(binding.apparatus))
			ability_fail(path + ".apparatus", "unknown fitted apparatus");
		if (binding.ability.empty())
		{
			if (!duty || duty->choir.empty() || binding.apparatus != duty->choir)
				ability_fail(path + ".ability", "dynamic ability selection requires a choir duty");
			for (const auto &id : psychic.choirs.at(binding.apparatus).powers)
			{
				const auto &ability = definitions.at(id);
				if (ability.energy_per_second || ability.preparation_energy_per_second)
					require_power(binding.apparatus, path + ".apparatus");
				for (const auto *group : {&ability.start_effect, &ability.activation_effect})
					if (*group && (*group)->energy)
						require_power((*group)->payer, path + ".payer");
			}
			for (const auto &o : personnel.officers)
				if (o.duty == binding.owner)
				{
					std::size_t compatible = 0;
					for (const auto &id : o.abilities)
						compatible += psychic.choirs.at(binding.apparatus).powers.contains(id);
					if (compatible > 1)
						ability_fail(path, "starting officer has ambiguous compatible abilities; bind a fixed ability");
				}
		}
		else
		{
			const auto found = definitions.find(binding.ability);
			if (found == definitions.end())
				ability_fail(path + ".ability", "unknown ability");
			if (found->second.target != "self" && binding.target == ability_no_channel)
				ability_fail(path + ".target", "targeted ability requires a target channel");
			if ((found->second.energy_per_second || found->second.preparation_energy_per_second ||
			     !found->second.psychic_effect.empty()) &&
			    binding.apparatus.empty() && binding.source != "equipment")
				ability_fail(path + ".apparatus", "funded/native psychic ability requires explicit apparatus");
			if (found->second.energy_per_second || found->second.preparation_energy_per_second)
				require_power(binding.apparatus.empty() ? binding.owner : binding.apparatus, path + ".apparatus");
			for (const auto *group : {&found->second.start_effect, &found->second.activation_effect})
				if (*group && (*group)->energy)
					require_power((*group)->payer, path + ".payer");
			if (!found->second.apparatus_recovery_key.empty() && binding.apparatus.empty() &&
			    binding.source != "equipment")
				ability_fail(path + ".apparatus", "apparatus recovery requires explicit apparatus");
			if (!found->second.psychic_effect.empty() &&
			    !psychic.choirs.contains(
			        binding.apparatus.empty() && binding.source == "equipment" ? binding.owner : binding.apparatus))
				ability_fail(path + ".apparatus", "psychic ability requires a psychic apparatus");
			if (!found->second.psychic_effect.empty() &&
			    !psychic.choirs
			         .at(binding.apparatus.empty() && binding.source == "equipment" ? binding.owner : binding.apparatus)
			         .powers.contains(binding.ability))
				ability_fail(path + ".ability", "psychic ability is unsupported by the bound apparatus");
			for (const auto &exclusion : found->second.exclusions)
				if (exclusion.scope == "apparatus" && binding.apparatus.empty() && binding.source != "equipment")
					ability_fail(path + ".apparatus", "apparatus exclusion requires explicit apparatus");
		}
		if (duty || officer)
		{
			const auto &department = duty ? duty->department : officer->department;
			const auto dep = std::find_if(personnel.departments.begin(), personnel.departments.end(),
			                              [&](const auto &d)
			                              {
				                              return d.id == department;
			                              });
			const auto consoles = resolve_consoles(catalog, entity);
			const auto console = std::find_if(consoles.begin(), consoles.end(),
			                                  [&](const auto &c)
			                                  {
				                                  return c.id == controller.console;
			                                  });
			if (dep == personnel.departments.end() || console == consoles.end() || dep->station != console->station)
				ability_fail(path, "ability Controller belongs to another Station");
		}
		if (binding.planner)
		{
			if (*binding.planner == binding.controller || *binding.planner >= layout.controllers.size() ||
			    layout.controllers[*binding.planner].console != controller.console)
				ability_fail(path + ".planner", "unknown same-Console planning Controller");
			const auto &planner = layout.controllers[*binding.planner];
			const auto planned = [&](std::size_t index, std::size_t planned_index)
			{
				if (index == ability_no_channel)
				{
					if (planned_index != ability_no_channel)
						ability_fail(path + ".planner", "planning target has no execution target");
					return;
				}
				if (planned_index >= planner.commands.size())
					ability_fail(path + ".planner", "missing matching planning channel");
				const auto &execution = controller.commands[index];
				const auto &preference = planner.commands[planned_index];
				for (const auto &[id, process] : process_controls.items())
					if (process.value("controller", std::string{}) == planner.id &&
					    process.value("enabled", std::string{}) == preference.id)
						ability_fail(path, "planning channel cannot drive both an ability and a process");
				const auto action = execution.action == OperatingAction::AbilityActivate
				                        ? OperatingAction::IntentBoolean
				                        : execution.action;
				if (preference.id != execution.id || preference.action != action ||
				    preference.minimum != execution.minimum || preference.maximum != execution.maximum ||
				    preference.initial != 0)
					ability_fail(path + ".planner", "planning channels must match ability intent");
			};
			planned(binding.enabled, binding.planning_enabled);
			planned(binding.target, binding.planning_target);
		}
		const auto apparatus =
		    binding.apparatus.empty() && binding.source == "equipment" ? binding.owner : binding.apparatus;
		const auto recovery_bound = [&](const std::string &ability)
		{
			const auto &definition = definitions.at(ability);
			if (definition.apparatus_recovery_ticks)
			{
				const auto key = "apparatus:" + apparatus + ":" + definition.apparatus_recovery_key;
				auto &bound = result.apparatus_recovery_bounds[key];
				bound = std::max(bound, definition.apparatus_recovery_ticks);
			}
		};
		if (!binding.ability.empty())
			recovery_bound(binding.ability);
		else
			for (const auto &ability : psychic.choirs.at(binding.apparatus).powers)
				recovery_bound(ability);
		result.controls.push_back(std::move(binding));
	};
	const auto channel_index = [&](std::size_t controller, const std::string &name, const std::string &path)
	{
		const auto &commands = layout.controllers.at(controller).commands;
		const auto found = std::find_if(commands.begin(), commands.end(),
		                                [&](const auto &command)
		                                {
			                                return command.id == name;
		                                });
		if (found == commands.end())
			ability_fail(path, "unknown ability command channel");
		return static_cast<std::size_t>(found - commands.begin());
	};
	const Json empty_controls = Json::object();
	const auto &controls = hull.contains("ability_controls") ? hull.at("ability_controls") : empty_controls;
	if (!controls.is_object() || controls.size() > 256)
		ability_fail(entity + ".ability_controls", "expected at most 256 named endpoints");
	for (const auto &[id, value] : controls.items())
	{
		const auto path = entity + ".ability_controls." + id;
		ability_fields(
		    value, {"source", "owner", "ability", "apparatus", "controller", "console", "enabled", "target", "planner"},
		    path);
		AbilityControlDefinition binding;
		binding.id = id;
		binding.source = value.value("source", std::string("equipment"));
		binding.owner = value.value("owner", std::string{});
		binding.ability = value.value("ability", std::string{});
		binding.apparatus = value.value("apparatus", std::string{});
		const auto controller = std::find_if(layout.controllers.begin(), layout.controllers.end(),
		                                     [&](const auto &c)
		                                     {
			                                     return c.id == value.at("controller").get<std::string>() &&
			                                            c.console == value.at("console").get<std::string>();
		                                     });
		if (controller == layout.controllers.end())
			ability_fail(path, "unknown Controller/Console binding");
		binding.controller = static_cast<std::size_t>(controller - layout.controllers.begin());
		binding.enabled = channel_index(binding.controller, value.at("enabled").get<std::string>(), path + ".enabled");
		if (value.contains("target"))
			binding.target = channel_index(binding.controller, value.at("target").get<std::string>(), path + ".target");
		const auto planner_id = value.value("planner", std::string{});
		if (!planner_id.empty())
		{
			const auto planner = std::find_if(layout.controllers.begin(), layout.controllers.end(),
			                                  [&](const auto &c)
			                                  {
				                                  return c.id == planner_id && c.console == controller->console;
			                                  });
			if (planner == layout.controllers.end())
				ability_fail(path + ".planner", "unknown same-Console planning Controller");
			binding.planner = static_cast<std::size_t>(planner - layout.controllers.begin());
			binding.planning_enabled =
			    channel_index(*binding.planner, controller->commands[binding.enabled].id, path + ".planner");
			if (binding.target != ability_no_channel)
				binding.planning_target =
				    channel_index(*binding.planner, controller->commands[binding.target].id, path + ".planner");
		}
		add(std::move(binding), path);
	}
	for (const auto &[id, choir] : psychic.choirs)
		for (std::size_t i = 0; i < choir.seats.size(); ++i)
		{
			const auto &seat = choir.seats[i];
			AbilityControlDefinition binding;
			binding.id = seat.control;
			binding.source = "duty";
			binding.owner = seat.duty;
			binding.ability = seat.ability;
			binding.apparatus = id;
			binding.controller = seat.controller;
			binding.enabled = seat.enabled;
			binding.target = seat.target;
			binding.planner = seat.planner;
			binding.planning_enabled = seat.planner ? seat.planning_enabled : ability_no_channel;
			binding.planning_target = seat.planner ? seat.planning_target : ability_no_channel;
			add(std::move(binding), entity + ".systems." + id + ".seats." + std::to_string(i));
		}
	if (result.controls.size() > 256)
		ability_fail(entity + ".ability_controls", "combined endpoint limit is 256");
	for (std::size_t controller = 0; controller < layout.controllers.size(); ++controller)
		for (std::size_t channel = 0; channel < layout.controllers[controller].commands.size(); ++channel)
			if (layout.controllers[controller].commands[channel].action == OperatingAction::AbilityActivate &&
			    std::none_of(result.controls.begin(), result.controls.end(),
			                 [&](const auto &control)
			                 {
				                 return control.controller == controller && control.enabled == channel;
			                 }))
				ability_fail(entity + ".ability_controls",
				             "ability activation request requires exactly one declared endpoint executor");
	return result;
}
} // namespace phoenix
