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
module phoenix.content;
namespace phoenix
{
namespace
{
[[noreturn]] void process_fail(const std::string &at, const std::string &why)
{
	throw std::runtime_error(at + ": " + why);
}
void process_fields(const Json &v, const std::set<std::string> &allowed, const std::string &at)
{
	if (!v.is_object())
		process_fail(at, "expected object");
	for (const auto &[key, unused] : v.items())
		if (!allowed.contains(key))
			process_fail(at + "." + key, "unsupported field");
}
NativeProcessKind native_process_kind(const std::string &name, const std::string &at)
{
	static const std::map<std::string, NativeProcessKind> kinds{{"effects", NativeProcessKind::Effects},
	                                                            {"generation", NativeProcessKind::Generation},
	                                                            {"propulsion", NativeProcessKind::Propulsion},
	                                                            {"cooling", NativeProcessKind::Cooling},
	                                                            {"processor", NativeProcessKind::Processor}};
	const auto found = kinds.find(name);
	if (found == kinds.end())
		process_fail(at, "unknown native process kind");
	return found->second;
}
} // namespace
ProcessDefinition compile_process(const Catalog &catalog, const std::string &id, SharedPropertyCatalog supplied)
{
	const auto properties = supplied ? std::move(supplied) : compile_property_catalog(catalog);
	const auto found = catalog.effects.find(id);
	const auto at = "effects." + id;
	if (found == catalog.effects.end() || found->second.value("kind", std::string{}) != "process")
		process_fail(at, "unknown process template");
	const auto &value = found->second;
	process_fields(value,
	               {"id",           "kind",          "extends",     "name",         "label",        "family",
	                "native",       "requested",     "shortage",    "work_unit",    "requirements", "rate",
	                "upkeep_scale", "work_scale",    "entry_costs", "upkeep_costs", "work_costs",   "start_effect",
	                "pause_effect", "resume_effect", "stop_effect", "work_effect",  "work_values",  "exclusions",
	                "observations", "rules"},
	               at);
	ProcessDefinition out;
	out.id = id;
	out.label = value.value("label", id);
	out.family = value.value("family", std::string{});
	if (!out.family.empty() && !valid_identifier(out.family))
		process_fail(at, "invalid family");
	if (out.label.empty() || out.label.size() > 128)
		process_fail(at, "invalid label");
	out.native = native_process_kind(value.value("native", std::string("effects")), at);
	if (value.contains("requested") && !value.at("requested").is_boolean())
		process_fail(at, "requested must be Boolean");
	out.requested = value.value("requested", true);
	out.shortage = value.value("shortage", std::string("pause"));
	if (out.shortage != "pause" && out.shortage != "stop" && out.shortage != "proportional")
		process_fail(at, "unknown shortage policy");
	switch (out.native)
	{
	case NativeProcessKind::Effects:
		out.work_unit = "work";
		out.work_divisor = 1000;
		break;
	case NativeProcessKind::Generation:
		out.work_unit = "energy";
		out.work_divisor = 1;
		break;
	case NativeProcessKind::Cooling:
		out.work_unit = "heat";
		out.work_divisor = 1;
		break;
	case NativeProcessKind::Propulsion:
		out.work_unit = "effort_milliticks";
		out.work_divisor = 1000;
		break;
	case NativeProcessKind::Processor:
		out.work_unit = "work_milliunits";
		out.work_divisor = 1000;
		break;
	}
	if (value.value("work_unit", out.work_unit) != out.work_unit)
		process_fail(at, "work_unit is fixed by native mechanism");
	out.evaluation.id = id;
	out.evaluation.label = out.label;
	out.evaluation.work_unit = out.work_unit;
	const auto expression = [&](const char *key, state_tree::Type type,
	                            const std::string &unit) -> std::optional<RuleExpression>
	{
		if (!value.contains(key))
			return {};
		auto compiled = compile_rule_expression(catalog, out.evaluation, value.at(key), at + "." + key, properties);
		if (compiled.type != type || compiled.unit != unit)
			process_fail(at + "." + key, "incompatible expression type or unit");
		for (const auto &fact : out.evaluation.facts)
			if (fact.kind == RuleFactKind::ProcessWork || fact.kind == RuleFactKind::ProcessActivity)
				process_fail(at + "." + key, "completed work is only available to work_values");
		return compiled;
	};
	out.evaluation.condition = expression("requirements", state_tree::Type::Boolean, {});
	out.rate = expression("rate", state_tree::Type::Integer, "permille");
	out.upkeep_scale = expression("upkeep_scale", state_tree::Type::Integer, "permille");
	out.work_scale = expression("work_scale", state_tree::Type::Integer, "permille");
	const auto costs = [&](const char *key, const char *quantity)
	{
		return !value.contains(key) || (value.at(key).is_array() && value.at(key).empty())
		           ? std::vector<EffectCostDefinition>{}
		           : compile_effect_costs(catalog, value.at(key), at + "." + key, quantity, properties);
	};
	out.entry_costs = costs("entry_costs", "amount");
	out.upkeep_costs = costs("upkeep_costs", "per_second");
	out.work_costs = costs("work_costs", "per_work");
	const auto group = [&](const char *key) -> std::optional<EffectGroupDefinition>
	{
		if (!value.contains(key))
			return {};
		if (!value.at(key).is_string())
			process_fail(at + "." + key, "expected operations template reference");
		return compile_effect_group(catalog, value.at(key).get<std::string>(), properties);
	};
	out.start_effect = group("start_effect");
	out.pause_effect = group("pause_effect");
	out.resume_effect = group("resume_effect");
	out.stop_effect = group("stop_effect");
	if (const auto work = group("work_effect"))
		out.evaluation.effects = *work;
	if (value.contains("work_values"))
	{
		if (!value.contains("work_effect"))
			process_fail(at, "work_values requires work_effect");
		compile_rule_values(catalog, out.evaluation, value.at("work_values"), at + ".work_values", properties);
	}
	const auto exclusions = value.value("exclusions", Json::array());
	if (!exclusions.is_array() || exclusions.size() > 16)
		process_fail(at, "expected at most 16 exclusions");
	std::set<std::string> unique;
	for (const auto &entry : exclusions)
	{
		process_fields(entry, {"id", "scope"}, at + ".exclusions");
		AbilityExclusionDefinition item{entry.value("id", std::string{}), entry.value("scope", std::string("source"))};
		if (!valid_identifier(item.id) || item.scope != "source" || !unique.insert(item.id).second)
			process_fail(at, "exclusions require distinct source-scoped groups");
		out.exclusions.push_back(std::move(item));
	}
	const auto observations = value.value("observations", Json::array());
	if (!observations.is_array() || observations.size() > 256)
		process_fail(at, "expected bounded observations");
	unique.clear();
	for (const auto &ref : observations)
	{
		if (!ref.is_string() || !properties->contains(ref.get<std::string>()) ||
		    !unique.insert(ref.get<std::string>()).second)
			process_fail(at, "unknown or duplicate property observation");
		out.observations.push_back(ref.get<std::string>());
	}
	return out;
}
SharedProcessCatalog compile_process_catalog(const Catalog &catalog, SharedPropertyCatalog supplied)
{
	const auto properties = supplied ? std::move(supplied) : compile_property_catalog(catalog);
	ProcessCatalog out;
	for (const auto &[id, value] : catalog.effects)
		if (value.value("kind", std::string{}) == "process")
		{
			if (out.size() >= 1024)
				process_fail("processes", "too many templates");
			out.emplace(id, compile_process(catalog, id, properties));
		}
	return std::make_shared<const ProcessCatalog>(std::move(out));
}
ProcessGrantDefinition compile_process_grant(const Catalog &catalog, const std::string &id)
{
	const auto at = "effects." + id;
	const auto found = catalog.effects.find(id);
	if (found == catalog.effects.end() || found->second.value("kind", std::string{}) != "process_grant")
		process_fail(at, "unknown process grant");
	const auto &value = found->second;
	process_fields(value, {"id", "kind", "extends", "name", "label", "process", "native_id", "duration_ticks", "rules"},
	               at);
	ProcessGrantDefinition out;
	out.process = value.value("process", std::string{});
	out.native_id = value.value("native_id", std::string{});
	const auto process = catalog.effects.find(out.process);
	if (process == catalog.effects.end() || process->second.value("kind", std::string{}) != "process" ||
	    (!out.native_id.empty() && !valid_identifier(out.native_id)))
		process_fail(at, "invalid process/native identity");
	const auto duration = value.value("duration_ticks", Json(0));
	if (!duration.is_number_integer() || duration < 0 || duration > 1000000)
		process_fail(at, "invalid duration");
	out.duration_ticks = duration.get<std::int64_t>();
	return out;
}
std::string resolve_process_native_binding(const ProcessDefinition &definition, const ProcessNativeBindings &bindings,
                                           const std::string &requested_id)
{
	if (definition.native == NativeProcessKind::Effects)
	{
		if (!requested_id.empty())
			process_fail(definition.id, "effect-only process omits native_id");
		return {};
	}
	const auto found = bindings.find(definition.native);
	if (found == bindings.end() || found->second.empty())
		process_fail(definition.id, "native process requires matching fitted machinery");
	if (!requested_id.empty())
	{
		if (!found->second.contains(requested_id))
			process_fail(definition.id, "native_id does not belong to the owning equipment/mechanism");
		return requested_id;
	}
	if (found->second.size() != 1)
		process_fail(definition.id, "ambiguous native machinery binding; set native_id");
	return *found->second.begin();
}
ProcessDefinitionSet compile_process_bindings(const Catalog &catalog, const std::string &entity,
                                              SharedProcessCatalog processes, SharedPropertyCatalog supplied)
{
	const auto properties = supplied ? std::move(supplied) : compile_property_catalog(catalog);
	if (!processes)
		process_fail(entity, "compiled process catalogue required");
	ProcessDefinitionSet out;
	out.catalog = std::move(processes);
	const auto &hull = catalog.entities.at(entity);
	if (catalog.presentation)
		return out;
	// Freeze the capabilities of the resolved fit, including equipment without static declarations.
	// Dynamic grants and save restoration resolve against these same immutable capabilities.
	for (const auto &[id, owner] : hull.at("systems").items())
	{
		auto &native = out.native_bindings[id];
		const auto kind = owner.value("kind", id);
		const auto add = [&](NativeProcessKind mechanism, const std::string &native_id)
		{
			if (!native_id.empty())
				native[mechanism].insert(native_id);
		};
		if (kind == "generator")
			add(NativeProcessKind::Generation, owner.value("generator", std::string{}));
		if (kind == "coolant")
			add(NativeProcessKind::Cooling, owner.value("cooling_link", std::string{}));
		if (kind == "engine" || kind == "steering" || kind == "lateral" || kind == "boost" || kind == "impulse")
			add(NativeProcessKind::Propulsion, id);
		if (owner.contains("processor"))
			add(NativeProcessKind::Processor, id);
		for (const auto &processor : hull.value("processors", Json::array()))
			if (processor.value("equipment", std::string{}) == id)
				add(NativeProcessKind::Processor, processor.at("id").get<std::string>());
	}
	std::size_t count = 0;
	const auto bindings =
	    [&](const Json &owner, const std::string &owner_kind, const std::string &owner_id, const std::string &path)
	{
		std::vector<ProcessBinding> result;
		const auto rows = owner.value("processes", Json::object());
		if (!rows.is_object() || rows.size() > 64)
			process_fail(path, "expected at most 64 named processes");
		for (const auto &[slot, value] : rows.items())
		{
			process_fields(value, {"process", "requested", "native_id"}, path + "." + slot);
			ProcessBinding b;
			b.id = slot;
			b.process = value.value("process", std::string{});
			b.native_id = value.value("native_id", std::string{});
			if (!valid_identifier(slot) || !out.catalog->contains(b.process) || ++count > 4096)
				process_fail(path, "invalid slot or process reference");
			const auto &definition = out.catalog->at(b.process);
			if (value.contains("requested") && !value.at("requested").is_boolean())
				process_fail(path, "requested must be Boolean");
			b.requested = value.value("requested", definition.requested);
			if (definition.native != NativeProcessKind::Effects && owner_kind != "equipment")
				process_fail(path, "native machinery processes require equipment owners");
			b.native_id = resolve_process_native_binding(
			    definition, owner_kind == "equipment" ? out.native_bindings.at(owner_id) : ProcessNativeBindings{},
			    b.native_id);
			result.push_back(std::move(b));
		}
		return result;
	};
	out.entity = bindings(hull, "entity", entity, "entities." + entity + ".processes");
	for (const auto &[id, system] : hull.at("systems").items())
		out.equipment.emplace(
		    id, bindings(system, "equipment", id, "entities." + entity + ".systems." + id + ".processes"));
	if (hull.contains("personnel") && !hull.at("personnel").is_null())
		for (const auto &officer : hull.at("personnel").at("officers"))
		{
			const auto id = officer.at("id").get<std::string>();
			out.officers.emplace(
			    id, bindings(officer, "officer", id, "entities." + entity + ".officers." + id + ".processes"));
		}
	if (hull.contains("combat"))
		for (const auto &deck : hull.at("combat").at("decks"))
			for (const auto &room : deck.at("compartments"))
			{
				const auto id = room.at("id").get<std::string>();
				out.compartments.emplace(
				    id, bindings(room, "compartment", id, "entities." + entity + ".compartments." + id + ".processes"));
			}
	const auto controls = hull.value("process_controls", Json::object());
	if (!controls.is_object() || controls.size() > 256)
		process_fail(entity + ".process_controls", "expected bounded object");
	if (controls.empty())
		return out;
	const auto layout = compile_controllable_layout(catalog, entity, properties);
	const auto consoles = resolve_consoles(catalog, entity);
	std::set<std::pair<std::size_t, std::size_t>> channels;
	for (const auto &[id, value] : controls.items())
	{
		process_fields(value, {"source", "owner", "process", "controller", "enabled"},
		               entity + ".process_controls." + id);
		ProcessControlDefinition c;
		c.id = id;
		c.source = value.value("source", std::string("equipment"));
		c.owner = value.value("owner", std::string{});
		c.process = value.value("process", std::string{});
		if (!valid_identifier(id) || !valid_identifier(c.process))
			process_fail(entity, "invalid process endpoint/slot");
		const bool exists = c.source == "entity"        ? c.owner.empty()
		                    : c.source == "equipment"   ? out.equipment.contains(c.owner)
		                    : c.source == "officer"     ? out.officers.contains(c.owner)
		                    : c.source == "compartment" ? out.compartments.contains(c.owner)
		                                                : false;
		if (!exists)
			process_fail(entity, "unknown process owner");
		const auto controller = value.value("controller", std::string{}),
		           channel = value.value("enabled", std::string{});
		const auto found = std::find_if(layout.controllers.begin(), layout.controllers.end(),
		                                [&](const auto &row)
		                                {
			                                return row.id == controller;
		                                });
		if (found == layout.controllers.end())
			process_fail(entity, "unknown process Controller");
		c.controller = static_cast<std::size_t>(found - layout.controllers.begin());
		const auto command = std::find_if(found->commands.begin(), found->commands.end(),
		                                  [&](const auto &row)
		                                  {
			                                  return row.id == channel;
		                                  });
		if (command == found->commands.end() || command->action != OperatingAction::IntentBoolean ||
		    command->minimum != 0 || command->maximum != 1 || command->semantics != CommandSemantics::Setting)
			process_fail(entity, "process enabled control requires Boolean intent");
		const auto &declared = c.source == "entity"      ? out.entity
		                       : c.source == "equipment" ? out.equipment.at(c.owner)
		                       : c.source == "officer"   ? out.officers.at(c.owner)
		                                                 : out.compartments.at(c.owner);
		for (const auto &binding : declared)
			if (binding.id == c.process && bool(command->initial) != binding.requested)
				process_fail(entity, "process control initial intent conflicts with declared requested state");
		c.enabled = static_cast<std::size_t>(command - found->commands.begin());
		if (!channels.emplace(c.controller, c.enabled).second)
			process_fail(entity, "process channel has multiple consumers");
		if (c.source == "equipment")
		{
			const auto console = std::find_if(consoles.begin(), consoles.end(),
			                                  [&](const auto &row)
			                                  {
				                                  return row.id == found->console;
			                                  });
			if (console == consoles.end() || std::find(console->feed_systems.begin(), console->feed_systems.end(),
			                                           c.owner) == console->feed_systems.end())
				process_fail(entity, "process Controller lacks owner equipment feed");
		}
		if (c.source == "officer")
		{
			const auto &personnel = hull.at("personnel");
			std::string department;
			for (const auto &officer : personnel.at("officers"))
				if (officer.at("id") == c.owner)
					department = officer.at("department").get<std::string>();
			std::string station;
			for (const auto &d : personnel.at("departments"))
				if (d.at("id") == department)
					station = d.at("station").get<std::string>();
			const auto console = std::find_if(consoles.begin(), consoles.end(),
			                                  [&](const auto &row)
			                                  {
				                                  return row.id == found->console;
			                                  });
			if (console == consoles.end() || console->station != station)
				process_fail(entity, "process Controller belongs to another personnel Station");
		}
		out.controls.push_back(std::move(c));
	}
	return out;
}
} // namespace phoenix
