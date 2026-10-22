module;
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>
#include <cmath>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

module phoenix.content;

#include "knowledge_overlay.hpp"

namespace phoenix
{
std::vector<std::string> pack_dependencies(const Json &manifest)
{
	if (manifest.contains("parent"))
		return {manifest.at("parent").get<std::string>()};
	return manifest.value("dependencies", std::vector<std::string>{});
}
bool legacy_phoenix_root(const Json &manifest)
{
	return manifest.value("id", std::string{}) == "phoenix-base" && !manifest.contains("base") &&
	       !manifest.contains("parent") && pack_dependencies(manifest).empty() && !manifest.contains("format");
}
BaseConfiguration base_configuration(const Pack &pack)
{
	const auto &manifest = pack.manifest();
	if ((!manifest.contains("base") && !legacy_phoenix_root(manifest)) || manifest.contains("parent") ||
	    !pack_dependencies(manifest).empty())
		throw std::runtime_error("Launch base must be an independent root pack");
	const auto config = manifest.value("base", Json::object());
	return {manifest.at("id"), pack.hash(), config.value("render_mode", std::string("sprites")),
	        config.value("interface_style", std::string("current"))};
}
Json launcher_catalog(const Catalog &catalog)
{
	Json result{{"schema", 1}, {"scenarios", Json::array()}};
	result["base"] = {{"id", catalog.base.id},
	                  {"hash", catalog.base.hash},
	                  {"render_mode", catalog.base.render_mode},
	                  {"interface_style", catalog.base.interface_style}};
	for (const auto &[id, map] : catalog.maps)
	{
		Json scenario{{"id", id}, {"name", map.value("name", id)}, {"slots", Json::array()}};
		for (const auto &slot : map.at("ship_slots"))
		{
			Json entry{
			    {"id", slot.at("id")},
			    {"mandatory", slot.value("mandatory", false)},
			    {"default_ship", slot.value("default_ship", slot.at("allowed_ships").front().get<std::string>())},
			    {"ships", Json::array()}};
			for (const auto &ship : slot.at("allowed_ships"))
			{
				const auto ship_id = ship.get<std::string>();
				entry["ships"].push_back(
				    {{"id", ship_id}, {"name", catalog.entities.at(ship_id).value("name", ship_id)}});
			}
			scenario["slots"].push_back(std::move(entry));
		}
		result["scenarios"].push_back(std::move(scenario));
	}
	return result;
}
namespace
{
[[noreturn]] void invalid(const std::string &path, const std::string &why)
{
	throw std::runtime_error(path + ": " + why);
}
std::string text(const Json &object, const char *key, const std::string &path)
{
	if (!object.is_object() || !object.contains(key) || !object[key].is_string() ||
	    object[key].get<std::string>().empty())
		invalid(path + "." + key, "expected nonempty string");
	return object[key].get<std::string>();
}
std::int64_t integer(const Json &object, const char *key, const std::string &path, std::int64_t minimum,
                     std::int64_t maximum = 1000000000)
{
	if (!object.contains(key))
		invalid(path + "." + key, "expected integer");
	return checked_integer(object.at(key), minimum, maximum, path + "." + key);
}
void transform(const Json &value, const std::string &path)
{
	integer(value, "x_mm", path, -1000000000);
	integer(value, "y_mm", path, -1000000000);
	integer(value, "heading_mdeg", path, 0, 359999);
}
} // namespace
void inherit_definitions(std::map<std::string, Json> &definitions, const std::string &kind, const std::string &root)
{
	std::set<std::string> done, active;
	std::function<void(const std::string &)> visit = [&](const std::string &id)
	{
		if (done.contains(id))
			return;
		if (!active.insert(id).second)
			invalid(kind + "." + id, "inheritance cycle");
		auto &definition = definitions.at(id);
		if (definition.contains("extends"))
		{
			auto parent = text(definition, "extends", kind + "." + id);
			if (!definitions.contains(parent))
				invalid(kind + "." + id + ".extends", "unknown parent '" + parent + "'");
			visit(parent);
			auto resolved = definitions.at(parent);
			resolved.merge_patch(definition);
			resolved.erase("extends");
			definition = std::move(resolved);
		}
		active.erase(id);
		done.insert(id);
	};
	if (!root.empty())
		visit(root);
	else
		for (const auto &[id, value] : definitions)
			visit(id);
}
namespace
{
std::optional<state_tree::Context> contextual_tree(const std::map<std::string, Json> &trees, const std::string &id)
{
	std::set<std::string> seen;
	auto name = id;
	while (true)
	{
		if (!seen.insert(name).second || !trees.contains(name))
			invalid("trees." + id, "unresolved parent or inheritance cycle");
		const auto &tree = trees.at(name);
		if (tree.contains("context"))
		{
			const auto context = tree.at("context").get<std::string>();
			if (context != "system" && context != "scenario" && context != "controller" && context != "conversation" &&
			    context != "away" && context != "team")
				invalid("trees." + id, "unsupported tree context");
			return context == "team"           ? state_tree::Context::Team
			       : context == "away"         ? state_tree::Context::Away
			       : context == "conversation" ? state_tree::Context::Conversation
			       : context == "controller"   ? state_tree::Context::Controller
			       : context == "scenario"     ? state_tree::Context::Scenario
			                                   : state_tree::Context::System;
		}
		if (!tree.contains("extends"))
			return {};
		name = tree.at("extends").get<std::string>();
	}
}

} // namespace
namespace
{
void prepare_catalog(Catalog &catalog);
struct PackView
{
	const Json &manifest;
	const std::map<std::string, Bytes> &files;
	std::string hash;
};
Catalog resolve(const std::vector<PackView> &ordered)
{
	Catalog result;
	const bool legacy_tree = !ordered.empty() && legacy_phoenix_root(ordered.front().manifest);
	if (!ordered.empty() && (ordered.front().manifest.contains("base") || legacy_tree))
	{
		const auto &root = ordered.front();
		result.base = {root.manifest.at("id"), root.hash,
		               root.manifest.value("base", Json::object()).value("render_mode", std::string("sprites")),
		               root.manifest.value("base", Json::object()).value("interface_style", std::string("current"))};
	}
	std::set<std::string> loaded, global_ids;
	for (const auto &pack : ordered)
	{
		const auto format = pack.manifest.value("format", std::string{});
		if (format == "phoenix-console-assets" || format == "phoenix-console-selection")
		{
			if (ordered.size() != 1)
				invalid("pack", "Console assets cannot be mixed with gameplay packs");
			result.presentation = true;
			result.selection = format == "phoenix-console-selection";
			if (pack.manifest.contains("presentation_base"))
			{
				const auto &root = pack.manifest.at("presentation_base");
				const auto mode = root.at("render_mode").get<std::string>();
				if ((mode != "sprites" && mode != "models") || root.at("interface_style") != "current")
					invalid("presentation_base", "unsupported presentation");
				result.base = {root.at("id"), root.at("hash"), mode, "current"};
			}
		}
		if (format == "phoenix-runtime-save" || format == "phoenix-fleet-setup")
			invalid("pack", "session/setup containers are not gameplay packs; use their dedicated loader");
		auto id = text(pack.manifest, "id", "manifest");
		if (!result.presentation && !legacy_tree && !result.base.id.empty() && id != result.base.id &&
		    !pack.manifest.contains("parent"))
			invalid(id, "child pack must declare a parent in the launched base tree");
		if (!loaded.insert(id).second)
			invalid(id, "duplicate pack id");
		for (const auto &dependency : pack_dependencies(pack.manifest))
		{
			if (dependency == id || !loaded.contains(dependency))
				invalid(id + ".dependencies", "dependency must appear earlier in the selected pack order");
		}
		if (pack.manifest.contains("loading_background"))
			result.loading_background = pack.manifest.at("loading_background").get<std::string>();
		auto definitions = parse_json(pack.files.at("definitions.json"), id + "/definitions.json");
		if (!definitions.is_object() || definitions.value("schema", 0) < 1 || definitions.value("schema", 0) > 4)
			invalid(id + "/definitions.json", "expected schema 1..4 object");
		std::set<std::string> overrides;
		for (const auto &override_id : pack.manifest.value("overrides", Json::array()))
			overrides.insert(override_id.get<std::string>());
		std::set<std::string> used, local;
		auto add = [&](const char *kind, std::map<std::string, Json> &target)
		{
			if (!definitions.contains(kind))
				return;
			if (!definitions[kind].is_array())
				invalid(id + "." + kind, "expected array");
			for (const auto &value : definitions[kind])
			{
				auto key = text(value, "id", id + "." + kind);
				if (!local.insert(key).second)
					invalid(id + "." + key, "duplicate definition in pack");
				if (global_ids.contains(key))
				{
					if (!target.contains(key) || !overrides.contains(key))
						invalid(id + "." + key, "duplicate definition requires explicit override of the same kind");
					used.insert(key);
				}
				global_ids.insert(key);
				target[key] = value;
			}
		};
		add("entities", result.entities);
		add("maps", result.maps);
		add("consoles", result.consoles);
		add("trees", result.trees);
		add("equipment", result.equipment);
		add("assemblies", result.assemblies);
		add("messages", result.messages);
		if (definitions.contains("knowledge"))
		{
			const auto &knowledge = definitions.at("knowledge");
			if (!knowledge.is_object())
				invalid(id + ".knowledge", "expected object");
			if (!result.knowledge.contains("roles"))
				result.knowledge["roles"] = Json::array();
			if (!result.knowledge.contains("schemas"))
				result.knowledge["schemas"] = Json::array();
			for (const auto &schema : knowledge.value("schemas", Json::array()))
			{
				const auto key = "knowledge." + text(schema, "id", id + ".knowledge.schemas");
				auto &schemas = result.knowledge["schemas"];
				auto existing = std::find_if(schemas.begin(), schemas.end(),
				                             [&](const Json &value)
				                             {
					                             return value.at("id") == schema.at("id");
				                             });
				if (existing != schemas.end())
				{
					if (!overrides.contains(key))
						invalid(key, "schema replacement requires explicit override");
					used.insert(key);
				}
			}
			overlay_knowledge(result.knowledge, knowledge, id + ".knowledge");
		}
		if (overrides != used)
			invalid(id + ".overrides", "every declared override must replace an earlier definition");
		for (const auto &[path, bytes] : pack.files)
			if (path.starts_with("assets/"))
				result.assets[id + ":" + path] = bytes;
		if (!pack.hash.empty())
			result.ordered_packs.emplace_back(id, pack.hash);
	}
	inherit_definitions(result.entities, "entities");
	inherit_definitions(result.maps, "maps");
	inherit_definitions(result.consoles, "consoles");
	std::map<std::string, Json> legacy_trees;
	for (const auto &[name, tree] : result.trees)
		if (!contextual_tree(result.trees, name))
			legacy_trees.emplace(name, tree);
	inherit_definitions(legacy_trees, "trees");
	for (auto &[name, tree] : legacy_trees)
		result.trees[name] = std::move(tree);
	inherit_definitions(result.equipment, "equipment");
	inherit_definitions(result.assemblies, "assemblies");
	resolve_equipment(result);
	if (!result.presentation)
	{
		auto &roles = result.knowledge["roles"];
		if (roles.is_null())
			roles = Json::array();
		for (const auto *role : {"phoenix.crew", "phoenix.scenario"})
			if (std::find(roles.begin(), roles.end(), Json(role)) == roles.end())
				roles.push_back(role);
		auto &schemas = result.knowledge["schemas"];
		if (schemas.is_null())
			schemas = Json::array();
		for (const auto &schema : schemas)
			if (schema.at("id") == "phoenix.objective" || schema.at("id") == "phoenix.camera")
				invalid("knowledge." + schema.at("id").get<std::string>(), "reserved schema");
		schemas.push_back(
		    {{"id", "phoenix.waypoint"},
		     {"producers", {"phoenix.scenario"}},
		     {"readers", {"phoenix.crew"}},
		     {"ship_publishable", true},
		     {"fields", Json::array({{{"id", "active"}, {"type", "boolean"}, {"publishable", true}},
		                             {{"id", "reached"}, {"type", "boolean"}, {"publishable", true}},
		                             {{"id", "x"}, {"type", "integer"}, {"publishable", true}},
		                             {{"id", "y"}, {"type", "integer"}, {"publishable", true}},
		                             {{"id", "revision"}, {"type", "integer"}, {"publishable", true}}})}});
		schemas.push_back(
		    {{"id", "phoenix.objective"},
		     {"producers", {"phoenix.scenario"}},
		     {"readers", {"phoenix.crew"}},
		     {"ship_publishable", true},
		     {"fields",
		      Json::array(
		          {{{"id", "id"}, {"type", "integer"}, {"publishable", true}},
		           {{"id", "status"}, {"type", "text"}, {"publishable", true}},
		           {{"id", "title"}, {"type", "text"}, {"publishable", true}},
		           {{"id", "description"}, {"type", "text"}, {"publishable", true}},
		           {{"id", "outcome"}, {"type", "text"}, {"publishable", true}},
		           {{"id", "radius"}, {"type", "integer"}, {"publishable", true}, {"required", false}},
		           {{"id", "region"}, {"type", "text"}, {"publishable", true}, {"required", false}},
		           {{"id", "credited"}, {"type", "boolean"}, {"publishable", true}},
		           {{"id", "destination_x"}, {"type", "integer"}, {"publishable", true}, {"required", false}},
		           {{"id", "destination_y"}, {"type", "integer"}, {"publishable", true}, {"required", false}}})}});
	}
	if (!result.presentation)
		result.knowledge["schemas"].push_back(
		    {{"id", "phoenix.camera"},
		     {"producers", {"phoenix.scenario"}},
		     {"readers", {"phoenix.crew"}},
		     {"ship_publishable", true},
		     {"fields", Json::array({{{"id", "mode"}, {"type", "integer"}, {"publishable", true}},
		                             {{"id", "direction"}, {"type", "integer"}, {"publishable", true}}})}});
	if (!result.knowledge.empty())
	{
		if (!result.presentation)
		{
			result.knowledge["roles"].push_back("phoenix.officer");
			Json fields = Json::array();
			for (const auto *name : {"operation", "status", "reason"})
				fields.push_back({{"id", name}, {"type", "text"}, {"publishable", true}});
			for (const auto *name : {"request", "target", "amount", "priority", "expires"})
				fields.push_back({{"id", name}, {"type", "integer"}, {"publishable", true}});
			for (const auto &schema : result.knowledge["schemas"])
				if (schema.at("id") == "phoenix.coordination" || schema.at("id") == "phoenix.officer-decision")
					invalid("knowledge", "reserved coordination schema");
			result.knowledge["schemas"].push_back({{"id", "phoenix.coordination"},
			                                       {"producers", {"phoenix.officer"}},
			                                       {"readers", {"phoenix.crew"}},
			                                       {"ship_publishable", true},
			                                       {"fields", fields}});
		}
		if (!result.presentation)
			for (const auto *cap :
			     {"basic_identity", "contact_arcs", "shield_sectors", "compartment_condition", "findings"})
				result.knowledge["schemas"].push_back(
				    {{"id", std::string("phoenix.sensor.") + cap},
				     {"producers", {"phoenix.scenario"}},
				     {"readers", {"phoenix.crew"}},
				     {"ship_publishable", true},
				     {"fields", Json::array({{{"id", "payload"}, {"type", "text"}, {"publishable", true}}})}});
		if (!result.presentation)
			result.knowledge["schemas"].push_back(
			    {{"id", "phoenix.officer-decision"},
			     {"producers", {"phoenix.officer"}},
			     {"readers", {"phoenix.crew"}},
			     {"ship_publishable", true},
			     {"fields", Json::array({{{"id", "reason"}, {"type", "text"}, {"publishable", true}}})}});
		result.knowledge_definition = compile_knowledge(result.knowledge);
	}
	for (const auto &[name, entity] : result.entities)
		if (entity.contains("resources"))
			result.resource_definitions.emplace(name, compile_resource_network(entity.at("resources")));
	std::vector<std::string> entity_ids;
	for (const auto &[name, entity] : result.entities)
		entity_ids.push_back(name);
	for (const auto &[name, map] : result.maps)
		if (map.contains("scenario") || map.contains("regions") || map.contains("preload"))
		{
			std::vector<std::string> slots;
			for (const auto &slot : map.at("ship_slots"))
				slots.push_back(slot.at("id"));
			auto spawnable = entity_ids;
			if (map.contains("preload"))
			{
				spawnable.clear();
				for (const auto &id : scenario_definitions(result, name))
					if (result.entities.contains(id))
						spawnable.push_back(id);
			}
			result.scenarios.emplace(name, compile_scenario(map, spawnable, slots));
		}
	prepare_catalog(result);
	// Public portrait bytes use opaque content addresses without exposing character directories.
	for (const auto &[id, entity] : result.entities)
		for (const auto &person : entity.value("comms", Json::object()).value("characters", Json::array()))
		{
			const auto reference = person.value("portrait", std::string{});
			if (!reference.empty())
			{
				const auto png = result.assets.at(reference);
				result.assets["console-assets:assets/comms/" + sha256(png) + ".png"] = png;
			}
		}
	for (const auto &[id, entity] : result.entities)
		for (const auto &person : entity.value("personnel", Json::object()).value("officers", Json::array()))
		{
			const auto reference = person.value("portrait", std::string{});
			if (!reference.empty())
			{
				const auto png = result.assets.at(reference);
				result.assets["console-assets:assets/comms/" + sha256(png) + ".png"] = png;
			}
		}
	// Public icon aliases use identical addresses in full and restricted catalogues.
	for (const auto &[id, entity] : result.entities)
	{
		const auto reference = radar_sprite_reference(entity.at("render"));
		if (!reference.empty())
		{
			const auto &png = result.assets.at(reference);
			result.assets[radar_sprite_id(png)] = png;
		}
	}
	return result;
}
} // namespace
Catalog resolve_packs(const std::vector<Pack> &ordered)
{
	std::vector<PackView> views;
	for (const auto &pack : ordered)
		views.push_back({pack.manifest(), pack.files(), pack.hash()});
	return resolve(views);
}
Catalog resolve_draft(const std::vector<Pack> &libraries, const PackDraft &draft)
{
	verify_pack_draft(draft);
	std::vector<PackView> views;
	for (const auto &pack : libraries)
		views.push_back({pack.manifest(), pack.files(), pack.hash()});
	views.push_back({draft.manifest, draft.files, {}});
	return resolve(views);
}
std::span<const WidgetBinding> widget_bindings()
{
	static constexpr WidgetBinding bindings[] = {{"conversations", "ship.conversations"},
	                                             {"radar", "radar.contacts"},
	                                             {"throttle", "engine.desired_speed"},
	                                             {"heading", "steering.desired_heading"},
	                                             {"camera_mode", "ship.camera.mode"},
	                                             {"camera_direction", "ship.camera.direction"},
	                                             {"status", "ship.status"},
	                                             {"objectives", "ship.objectives"},
	                                             {"navigation", "ship.navigation"}};
	return bindings;
}
namespace
{
void verify_prepared_catalog(const Catalog &catalog)
{
	if (catalog.presentation &&
	    (!catalog.maps.empty() || !catalog.trees.empty() || !catalog.equipment.empty() || !catalog.assemblies.empty() ||
	     !catalog.messages.empty() || !catalog.knowledge.empty() ||
	     (catalog.selection ? (!catalog.entities.empty() || !catalog.consoles.empty() || !catalog.assets.empty())
	                        : catalog.entities.size() != 1)))
		invalid("presentation", "expected one own-ship definition without maps or controllers");
	if (!catalog.presentation)
		(void)compile_gameplay_knowledge(catalog);
	if (!catalog.loading_background.empty())
	{
		if (!catalog.assets.contains(catalog.loading_background))
			invalid("loading_background", "unresolved image");
		decode_asset_image(catalog.assets.at(catalog.loading_background));
	}
	std::set<std::string> verified_images;
	for (const auto &[id, entity] : catalog.entities)
	{
		auto path = "entities." + id;
		auto tags = [&](const Json &v, const std::string &where)
		{
			if (!v.is_array() || v.size() > 32)
				invalid(where, "expected at most 32 tags");
			std::set<std::string> seen;
			for (const auto &tag : v)
				if (!tag.is_string() || tag.get<std::string>().empty() || tag.get<std::string>().size() > 128 ||
				    !seen.insert(tag.get<std::string>()).second)
					invalid(where, "tags must be unique non-empty strings of at most 128 characters");
		};
		if (entity.contains("tags"))
			tags(entity.at("tags"), path + ".tags");
		if (entity.contains("systems"))
			for (const auto &[key, system] : entity.at("systems").items())
				if (system.contains("shows"))
					tags(system.at("shows"), path + ".systems." + key + ".shows");
		if (catalog.presentation)
			for (auto field = entity.begin(); field != entity.end(); ++field)
				if (field.key() != "id" && field.key() != "render" && field.key() != "systems" &&
				    field.key() != "console" && field.key() != "stations")
					invalid(path, "unsupported presentation field");
		if (!entity.contains("render") || !entity["render"].is_object())
			invalid(path + ".render", "required object");
		const auto &render = entity["render"];
		if (render.contains("reference_grid"))
			compile_reference_grid(render.at("reference_grid"), path + ".render.reference_grid");
		render_radar_icon(render);
		const auto radar_sprite = radar_sprite_reference(render);
		if (!radar_sprite.empty())
		{
			Json icon = {{"albedo", radar_sprite},
			             {"frames", Json::array({{{"name", "icon"}, {"rect", {0, 0, 1, 1}}, {"pivot", {0, 0}}}})}};
			compile_sprite(icon, catalog, path + ".render.radar_sprite");
		}
		const bool model = render.contains("model") || render.contains("star");
		const bool contact_only = render.value("contact_only", false);
		if (contact_only && !catalog.presentation)
			invalid(path + ".render", "contact-only visuals are restricted Console projections");
		if (!catalog.presentation && !catalog.base.id.empty() && model != (catalog.base.render_mode == "models"))
			invalid(path + ".render", "entity representation must match launch base");
		auto mode = (model || contact_only) ? std::string("camera") : text(render, "mode", path + ".render");
		if (mode != "camera" && mode != "eight")
			invalid(path + ".render.mode", "supported values are camera and eight");
		if (!render.contains("scale") || !render["scale"].is_number() ||
		    !std::isfinite(render["scale"].get<double>()) || render["scale"].get<double>() <= 0 ||
		    render["scale"].get<double>() > 1000000)
			invalid(path + ".render.scale", "expected positive finite scale <= 1000000");
		if (contact_only)
		{
			if (model || render.contains("images") || render.contains("sprite"))
				invalid(path + ".render", "contact-only projection cannot contain hull assets");
		}
		else if (model)
			verify_model_render(render, catalog, path + ".render");
		else if (render.contains("sprite"))
		{
			const auto material = compile_sprite(render.at("sprite"), catalog, path + ".render.sprite");
			sprite_entry_index(material, render.value("sprite_entry", std::string{}));
			if (material.entries.empty() && material.frames.size() != (mode == "eight" ? 8u : 1u))
				invalid(path + ".render.sprite.frames", "frame count does not match billboard mode");
			if (render.contains("engines"))
			{
				const auto &engines = render.at("engines");
				const auto effect = compile_sprite(engines.at("sprite"), catalog, path + ".render.engines.sprite");
				auto check_frames = [&](const auto &frames)
				{
					if (effect.frames.size() != frames.size())
						invalid(path + ".render.engines", "thrust views must match every parent entry");
					for (const auto &frame : frames)
						if (std::none_of(effect.frames.begin(), effect.frames.end(),
						                 [&](const auto &f)
						                 {
							                 return f.name == frame.name;
						                 }))
							invalid(path + ".render.engines", "missing named thrust view");
				};
				if (material.entries.empty())
					check_frames(material.frames);
				else
					for (const auto &entry : material.entries)
						check_frames(entry.frames);
				for (const char *key : {"scale", "power", "range"})
				{
					if (!engines.contains(key) || !engines.at(key).is_number())
						invalid(path + ".render.engines." + key, "required finite positive number");
					const auto n = engines.at(key).get<double>();
					if (!std::isfinite(n) || n <= 0 || n > 1000000)
						invalid(path + ".render.engines." + key, "outside supported range");
				}
			}
		}
		else
		{
			if (!render.contains("images") || !render["images"].is_array() ||
			    render["images"].size() != (mode == "eight" ? 8 : 1))
				invalid(path + ".render.images", "image count does not match billboard mode");
			for (const auto &asset : render["images"])
			{
				if (!asset.is_string() || !catalog.assets.contains(asset.get<std::string>()))
					invalid(path + ".render.images", "unresolved image reference");
				if (!verified_images.insert(asset.get<std::string>()).second)
					continue;
				const auto &bytes = catalog.assets.at(asset.get<std::string>());
				decode_asset_image(bytes);
			}
		}
		if (!entity.contains("systems") || !entity["systems"].is_object())
			invalid(path + ".systems", "required object");
		for (auto it = entity["systems"].begin(); it != entity["systems"].end(); ++it)
		{
			const auto &system = it.value();
			auto field = path + ".systems." + it.key();
			if (!system.is_object())
				invalid(field, "expected object");
			const auto kind = system.value("kind", it.key());
			if (kind == "engine")
			{
				integer(system, "max_speed_mm_s", field, 0);
				if (!catalog.presentation)
					integer(system, "acceleration_mm_s2", field, 1);
			}
			else if (kind == "steering")
			{
				if (!catalog.presentation)
					integer(system, "turn_mdeg_s", field, 1, 360000);
			}
			else if (kind == "dispatch" || kind == "shuttle_bay")
			{
			}
			else if (kind == "displacement" || kind == "transport")
			{
				integer(system, "range_mm", field, 1, 1000000000);
				integer(system, "capacity", field, 1, 64);
				integer(system, "transfer_ticks", field, 1, 36000);
				if (!catalog.presentation)
				{
					integer(system, "energy_per_second", field,
					        system.value("mode", std::string{"displacement"}) == "displacement" ? 1 : 0);
					if ((system.value("mode", std::string{"displacement"}) == "displacement" &&
					     !system.contains("power_store")) ||
					    !system.contains("compartment"))
						invalid(field, "displacement requires power and compartment");
				}
			}
			else if (kind == "comms")
			{
				integer(system, "range_mm", field, 1, 1000000000);
				if (!catalog.presentation)
				{
					integer(system, "energy_per_second", field,
					        system.value("mode", std::string{"displacement"}) == "displacement" ? 1 : 0);
					if ((system.value("mode", std::string{"displacement"}) == "displacement" &&
					     !system.contains("power_store")))
						invalid(field, "Comms requires power_store");
				}
			}
			else if (kind == "radar")
				integer(system, "range_mm", field, 1);
			else if (kind == "lateral")
			{
				if (catalog.presentation)
					integer(system, "max_speed_mm_s", field, 1);
			}
			else if (kind == "boost" || kind == "impulse")
			{
				if (!catalog.presentation && !system.contains("targets"))
				{
					integer(system, "max_speed_mm_s", field, 1);
					integer(system, "acceleration_mm_s2", field, 1);
				}
			}
			else if (kind != "generator" && kind != "storage" && kind != "distribution" && kind != "coolant" &&
			         kind != "weapon" && kind != "turret" && kind != "magazine" && kind != "repair_team" &&
			         kind != "team" && kind != "shield" && kind != "ftl")
				invalid(field, "unsupported System capability");
		}
		if (!catalog.presentation)
		{
			if (!entity.contains("collision") || !entity["collision"].is_object())
				invalid(path + ".collision", "required object");
			const auto &collision = entity["collision"];
			auto shape = text(collision, "shape", path + ".collision");
			if (shape == "circle")
				integer(collision, "radius_mm", path + ".collision", 1);
			else if (shape == "box")
			{
				integer(collision, "half_width_mm", path + ".collision", 1);
				integer(collision, "half_length_mm", path + ".collision", 1);
			}
			else
				invalid(path + ".collision.shape", "supported values are circle and box");
			integer(collision, "mass_grams", path + ".collision", 0);
		}
		if (entity.contains("console") &&
		    (!entity["console"].is_string() || !catalog.consoles.contains(entity["console"].get<std::string>())))
			invalid(path + ".console", "unresolved console definition");
		if (entity.contains("controller") &&
		    (!entity["controller"].is_string() || !catalog.trees.contains(entity["controller"].get<std::string>())))
			invalid(path + ".controller", "unresolved state tree");
		resolve_consoles(catalog, id);
		verify_gameplay_fitting(catalog, id);
		(void)compile_ftl(catalog, id);
		(void)compile_transport(catalog, id);
		(void)compile_craft(catalog, id);
		(void)compile_craft_bays(catalog, id);
		(void)compile_combat(catalog, id);
		if (entity.contains("destruction"))
			(void)compile_destruction(entity.at("destruction"), catalog, path + ".destruction");
		if (entity.contains("durability"))
			integer(entity, "durability", path, 0, 1000000);
		if (entity.contains("half_height_mm"))
			integer(entity, "half_height_mm", path, 1, 1000000);
		(void)compile_controllable_layout(catalog, id);
		if (entity.contains("controllers"))
		{
			if (!entity.contains("stations") || !entity["controllers"].is_object())
				invalid(path + ".controllers", "per-System controllers require explicit Stations");
			for (auto it = entity["controllers"].begin(); it != entity["controllers"].end(); ++it)
			{
				if (!entity["systems"].contains(it.key()) || !it.value().is_string() ||
				    !catalog.trees.contains(it.value().get<std::string>()))
					invalid(path + ".controllers." + it.key(), "unresolved System or tree");
				if (const auto context = contextual_tree(catalog.trees, it.value().get<std::string>());
				    !context || *context != state_tree::Context::Controller)
					invalid(path + ".controllers." + it.key(), "expected Controller tree context");
			}
		}
	}
	for (const auto &[id, entity] : catalog.entities)
	{
		(void)compile_bridge(catalog, id);
		(void)compile_personnel(catalog, id);
		(void)compile_boarding(catalog, id);
	}
	for (const auto &[id, console] : catalog.consoles)
	{
		auto path = "consoles." + id;
		(void)compile_console_bridge(console, path);
		if (!console.contains("widgets") || !console["widgets"].is_array())
			invalid(path + ".widgets", "expected array");
		std::set<std::string> widget_ids;
		for (const auto &widget : console["widgets"])
		{
			auto widget_id = text(widget, "id", path);
			if (!widget_ids.insert(widget_id).second)
				invalid(path + "." + widget_id, "duplicate widget id");
			auto kind = text(widget, "kind", path + "." + widget_id);
			if (widget.contains("fire_arcs") && (kind != "radar" || !widget.at("fire_arcs").is_boolean()))
				invalid(path + "." + widget_id, "fire_arcs requires a radar boolean");
			if (widget.contains("sweep"))
			{
				const auto &sweep = widget.at("sweep");
				if (kind != "radar" || !sweep.is_object())
					invalid(path + "." + widget_id, "sweep settings require a radar widget");
				if (sweep.contains("enabled") && !sweep.at("enabled").is_boolean())
					invalid(path, "sweep enabled must be boolean");
				for (const auto &[key, limits] : std::map<std::string, std::pair<double, double>>{
				         {"duration_seconds", {.1, 30}}, {"speed_degrees_s", {1, 720}}})
					if (sweep.contains(key))
					{
						if (!sweep.at(key).is_number())
							invalid(path, "sweep " + key + " must be numeric");
						const auto value = sweep.at(key).get<double>();
						if (!std::isfinite(value) || value < limits.first || value > limits.second)
							invalid(path, "sweep " + key + " outside supported range");
					}
				if (sweep.contains("colour"))
				{
					const auto &colour = sweep.at("colour");
					if (!colour.is_array() || colour.size() != 4)
						invalid(path, "sweep colour requires RGBA");
					for (const auto &channel : colour)
						if (!channel.is_number() || !std::isfinite(channel.get<double>()) || channel < 0 || channel > 1)
							invalid(path, "sweep colour channels must be within 0..1");
				}
			}
			auto binding = widget.contains("controller")
			                   ? widget.value("binding", kind == "throttle" ? std::string("engine.desired_speed")
			                                                                : std::string("steering.desired_heading"))
			                   : text(widget, "binding", path + "." + widget_id);
			const auto bindings = widget_bindings();
			if (std::none_of(bindings.begin(), bindings.end(),
			                 [&](const auto &entry)
			                 {
				                 return kind == entry.kind && binding == entry.binding;
			                 }))
				invalid(path + "." + widget_id, "unsupported widget/binding combination");
			if ((kind == "camera_mode" || kind == "camera_direction") &&
			    (widget.contains("system") || widget.contains("controller") || widget.contains("channel")))
				invalid(path + "." + widget_id, "camera widgets use ship settings, not physical bindings");
			text(widget, "label", path + "." + widget_id);
			if (widget.contains("controller") || widget.contains("channel"))
			{
				if ((kind != "throttle" && kind != "heading" && kind != "navigation") || widget.contains("system") ||
				    !widget.contains("controller") || !widget.contains("channel"))
					invalid(path + "." + widget_id,
					        "Controller widgets require controller/channel and no System binding");
				for (const auto *field : {"controller", "channel"})
					if (!valid_identifier(text(widget, field, path + "." + widget_id)))
						invalid(path + "." + widget_id, "invalid Controller binding identity");
			}
			auto x = integer(widget, "x", path, 0, 1000), y = integer(widget, "y", path, 0, 1000);
			auto w = integer(widget, "width", path, 1, 1000), h = integer(widget, "height", path, 1, 1000);
			if (x + w > 1000 || y + h > 1000)
				invalid(path + "." + widget_id, "widget exceeds the 1000 by 1000 authored layout");
		}
	}
	for (const auto &[id, map] : catalog.maps)
	{
		if (catalog.scenarios.contains(id))
		{
			std::size_t fog_count = 0;
			for (const auto &region : catalog.scenarios.at(id).regions)
			{
				fog_count += region.nebula.has_value();
				if (fog_count > 32)
					invalid("maps." + id, "at most 32 nebula volumes are supported");
				if (!region.asteroids)
					continue;
				for (const auto *types : {&region.asteroids->types, &region.asteroids->decorative_types})
					for (const auto &type : *types)
					{
						const auto &entity = catalog.entities.at(type.definition);
						if (!entity.at("systems").empty() || !entity.at("render").contains("sprite"))
							invalid("maps." + id, "asteroid types require sprite artwork and no fitted Systems");
						sprite_entry_index(compile_sprite(entity.at("render").at("sprite"), catalog, type.definition),
						                   type.entry);
					}
			}
		}
		if (map.contains("presentation"))
		{
			const auto &p = map.at("presentation");
			if (!p.is_object())
				invalid("maps." + id + ".presentation", "expected object");
			if (p.contains("skybox"))
			{
				const auto path = "maps." + id + ".presentation.skybox";
				if (!p.at("skybox").is_string())
					invalid(path, "expected image reference or empty string");
				const auto reference = p.at("skybox").get<std::string>();
				if (!reference.empty())
				{
					if (!catalog.assets.contains(reference))
						invalid(path, "unresolved image");
					try
					{
						const auto image = decode_asset_image(catalog.assets.at(reference));
						if (image.width != image.height * 2 && image.height != image.width * 6)
							invalid(path, "expected 2:1 panorama or vertical six-face cube strip");
					}
					catch (const std::exception &error)
					{
						invalid(path, error.what());
					}
				}
			}
			if (p.contains("dust"))
				for (bool web : {false, true})
					compile_dust(p.at("dust"), catalog, web, "maps." + id + ".presentation.dust");
		}
		if (map.contains("lighting"))
			verify_lighting(map.at("lighting"), "maps." + id + ".lighting");
		auto path = "maps." + id;
		(void)scenario_assets(catalog, id);
		if (map.contains("loading_background"))
		{
			const auto reference = map.at("loading_background").get<std::string>();
			if (!catalog.assets.contains(reference))
				invalid(path + ".loading_background", "unresolved image");
			decode_asset_image(catalog.assets.at(reference));
		}
		if (!map.contains("entities") || !map["entities"].is_array() || !map.contains("ship_slots") ||
		    !map["ship_slots"].is_array())
			invalid(path, "entities and ship_slots arrays required");
		std::set<std::int64_t> entity_ids;
		const auto factions = compile_factions(map);
		std::set<std::string> slots;
		for (const auto &instance : map["entities"])
		{
			auto entity_id = integer(instance, "id", path, 1);
			if (!entity_ids.insert(entity_id).second)
				invalid(path, "duplicate entity instance id");
			auto definition = text(instance, "definition", path);
			if (!catalog.entities.contains(definition))
				invalid(path, "unknown entity definition '" + definition + "'");
			factions.validate(
			    instance.value("faction", catalog.entities.at(definition).value("faction", std::string{})));
			transform(instance, path + ".entity." + std::to_string(entity_id));
			if (instance.contains("sprite_entry"))
			{
				const auto &render = catalog.entities.at(definition).at("render");
				if (!render.contains("sprite"))
					invalid(path, "sprite_entry requires a sprite material");
				sprite_entry_index(compile_sprite(render.at("sprite"), catalog, path),
				                   instance.at("sprite_entry").get<std::string>());
			}
		}
		for (const auto &instance : map["entities"])
		{
			for (const auto &system : resolve_systems(catalog, instance.at("definition").get<std::string>()))
			{
				if (!catalog.tree_programs.contains(system.controller) ||
				    catalog.tree_programs.at(system.controller).context != state_tree::Context::System)
					continue;
				std::function<void(const state_tree::Program &)> check_follow = [&](const state_tree::Program &program)
				{
					for (const auto &state : program.states)
						for (const auto *tasks : {&state.entry, &state.running, &state.exit})
							for (const auto &task : *tasks)
							{
								if (task.kind != state_tree::TaskKind::Action ||
								    task.target != static_cast<state_tree::Index>(GameplayTreeAction::SetFollowContact))
									continue;
								auto known = [&](std::size_t index) -> std::optional<state_tree::Value>
								{
									const auto &binding = task.arguments.at(index);
									if (binding.source == state_tree::Source::Literal)
										return binding.literal;
									return {};
								};
								if (const auto target = known(1);
								    target && (!entity_ids.contains(target->number) ||
								               target->number == instance.at("id").get<std::int64_t>()))
									invalid(path, "following target must identify another placed entity");
								for (const auto index : {2u, 3u})
									if (const auto value = known(index);
									    value && (value->number <= 0 || value->number > 1000000000))
										invalid(path, "following distance and speed must be positive bounded values");
							}
					for (const auto &reference : program.references)
						check_follow(reference);
				};
				check_follow(catalog.tree_programs.at(system.controller));
			}
		}
		for (const auto &slot : map["ship_slots"])
		{
			if (slot.contains("mandatory") && !slot.at("mandatory").is_boolean())
				invalid(path + ".ship_slots", "mandatory must be boolean");
			auto slot_id = text(slot, "id", path + ".ship_slots");
			if (!slots.insert(slot_id).second)
				invalid(path, "duplicate ship slot '" + slot_id + "'");
			transform(slot, path + ".slot." + slot_id);
			if (!slot.contains("allowed_ships") || !slot["allowed_ships"].is_array() || slot["allowed_ships"].empty())
				invalid(path + ".slot." + slot_id, "allowed_ships must be nonempty");
			if (slot.contains("default_ship") && (!slot.at("default_ship").is_string() ||
			                                      std::find(slot["allowed_ships"].begin(), slot["allowed_ships"].end(),
			                                                slot.at("default_ship")) == slot["allowed_ships"].end()))
				invalid(path + ".slot." + slot_id, "default_ship must name an allowed ship");
			std::set<std::string> allowed;
			for (const auto &ship : slot["allowed_ships"])
			{
				if (!ship.is_string() || !catalog.entities.contains(ship.get<std::string>()) ||
				    !allowed.insert(ship.get<std::string>()).second)
					invalid(path + ".slot." + slot_id, "unknown or repeated allowed ship");
				const auto &definition = catalog.entities.at(ship.get<std::string>());
				factions.validate(slot.value("faction", definition.value("faction", std::string{})));
				const auto fitted = resolve_systems(catalog, ship.get<std::string>());
				const auto has = [&](OperatingSystem kind)
				{
					return std::any_of(fitted.begin(), fitted.end(),
					                   [&](const auto &s)
					                   {
						                   return s.kind == kind;
					                   });
				};
				if (!has(OperatingSystem::Engine) || !has(OperatingSystem::Steering) ||
				    definition["collision"]["mass_grams"].get<std::int64_t>() <= 0)
					invalid(path + ".slot." + slot_id, "allowed ship needs engine, steering and positive mass");
			}
		}
	}
}
void prepare_catalog(Catalog &catalog)
{
	expand_transport_fittings(catalog);
	expand_combat_policies(catalog);
	expand_officer_policies(catalog);
	catalog.tree_programs.clear();
	catalog.conversation_programs.clear();
	catalog.controller_bindings.clear();
	for (const auto &[id, tree] : catalog.trees)
		if (const auto context = contextual_tree(catalog.trees, id))
		{
			if (*context == state_tree::Context::Conversation)
			{
				auto program = compile_conversation(tree, catalog.trees);
				catalog.tree_programs.emplace(id, program.tree);
				catalog.conversation_programs.emplace(id, std::move(program));
			}
			else if (*context == state_tree::Context::Team)
				catalog.tree_programs.emplace(id, state_tree::compile(tree, team_registry(), catalog.trees));
			else if (*context == state_tree::Context::Away)
				catalog.tree_programs.emplace(id, state_tree::compile(tree, away_registry(tree), catalog.trees));
			else if (*context == state_tree::Context::Controller)
			{
				auto bindings = compile_controller_bindings(tree, catalog);
				(void)compile_controller_actions(tree, catalog, bindings.registry);
				(void)compile_controller_candidates(tree, catalog, bindings.registry);
				catalog.tree_programs.emplace(id, state_tree::compile(tree, bindings.registry, catalog.trees));
				catalog.controller_bindings.emplace(id, std::move(bindings));
			}
			else
				catalog.tree_programs.emplace(
				    id, state_tree::compile(tree, gameplay_registry(tree, catalog, *context), catalog.trees));
		}
		else
			invalid("trees." + id, "timed tree format retired; author a controller-context state tree");
	for (const auto &[id, map] : catalog.maps)
	{
		verify_scenario_authoring(catalog, map);
		(void)compile_away(catalog, map);
		(void)compile_sensor_findings(catalog, map);
	}
	verify_conversation_authoring(catalog);
	if (!catalog.presentation)
		for (const auto &[id, entity] : catalog.entities)
			(void)compile_sensors(catalog, id);
	verify_prepared_catalog(catalog);
}
} // namespace
Catalog prepared_catalog(const Catalog &catalog)
{
	auto candidate = catalog;
	prepare_catalog(candidate);
	return candidate;
}
void verify_catalog(const Catalog &catalog)
{
	(void)prepared_catalog(catalog);
}
} // namespace phoenix
