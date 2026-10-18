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

namespace phoenix
{
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
	if (!object.contains(key) || !object[key].is_number_integer())
		invalid(path + "." + key, "expected integer");
	auto value = object[key].get<std::int64_t>();
	if (value < minimum || value > maximum)
		invalid(path + "." + key, "integer outside supported range");
	return value;
}
void transform(const Json &value, const std::string &path)
{
	integer(value, "x_mm", path, -1000000000);
	integer(value, "y_mm", path, -1000000000);
	integer(value, "heading_mdeg", path, 0, 359999);
}
void inherit(std::map<std::string, Json> &definitions, const std::string &kind)
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
	for (const auto &[id, value] : definitions)
		visit(id);
}
} // namespace
namespace
{
struct PackView
{
	const Json &manifest;
	const std::map<std::string, Bytes> &files;
	std::string hash;
};
Catalog resolve(const std::vector<PackView> &ordered)
{
	Catalog result;
	std::set<std::string> loaded, global_ids;
	for (const auto &pack : ordered)
	{
		const auto format = pack.manifest.value("format", std::string{});
		if (format == "phoenix-console-assets")
		{
			if (ordered.size() != 1)
				invalid("pack", "Console assets cannot be mixed with gameplay packs");
			result.presentation = true;
		}
		if (format == "phoenix-runtime-save" || format == "phoenix-fleet-setup")
			invalid("pack", "session/setup containers are not gameplay packs; use their dedicated loader");
		auto id = text(pack.manifest, "id", "manifest");
		if (!loaded.insert(id).second)
			invalid(id, "duplicate pack id");
		for (const auto &dependency : pack.manifest.value("dependencies", Json::array()))
		{
			if (!dependency.is_string() || dependency == id || !loaded.contains(dependency.get<std::string>()))
				invalid(id + ".dependencies", "dependency must appear earlier in the selected pack order");
		}
		auto definitions = parse_json(pack.files.at("definitions.json"), id + "/definitions.json");
		if (!definitions.is_object() || definitions.value("schema", 0) < 1 || definitions.value("schema", 0) > 3)
			invalid(id + "/definitions.json", "expected schema 1, 2 or 3 object");
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
		if (overrides != used)
			invalid(id + ".overrides", "every declared override must replace an earlier definition");
		for (const auto &[path, bytes] : pack.files)
			if (path.starts_with("assets/"))
				result.assets[id + ":" + path] = bytes;
		if (!pack.hash.empty())
			result.ordered_packs.emplace_back(id, pack.hash);
	}
	inherit(result.entities, "entities");
	inherit(result.maps, "maps");
	inherit(result.consoles, "consoles");
	inherit(result.trees, "trees");
	verify_catalog(result);
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
	static constexpr WidgetBinding bindings[] = {{"radar", "radar.contacts"},
	                                             {"throttle", "engine.desired_speed"},
	                                             {"heading", "steering.desired_heading"},
	                                             {"status", "ship.status"}};
	return bindings;
}
void verify_catalog(const Catalog &catalog)
{
	if (catalog.presentation && (!catalog.maps.empty() || !catalog.trees.empty() || catalog.entities.size() != 1))
		invalid("presentation", "expected one own-ship definition without maps or controllers");
	std::set<std::string> verified_images;
	for (const auto &[id, tree] : catalog.trees)
		verify_tree(tree, "trees." + id);
	for (const auto &[id, entity] : catalog.entities)
	{
		auto path = "entities." + id;
		if (catalog.presentation)
			for (auto field = entity.begin(); field != entity.end(); ++field)
				if (field.key() != "id" && field.key() != "render" && field.key() != "systems" &&
				    field.key() != "console" && field.key() != "stations")
					invalid(path, "unsupported presentation field");
		if (!entity.contains("render") || !entity["render"].is_object())
			invalid(path + ".render", "required object");
		const auto &render = entity["render"];
		render_radar_icon(render);
		auto mode = text(render, "mode", path + ".render");
		if (mode != "camera" && mode != "eight")
			invalid(path + ".render.mode", "supported values are camera and eight");
		if (!render.contains("scale") || !render["scale"].is_number() ||
		    !std::isfinite(render["scale"].get<double>()) || render["scale"].get<double>() <= 0 ||
		    render["scale"].get<double>() > 1000000)
			invalid(path + ".render.scale", "expected positive finite scale <= 1000000");
		if (render.contains("sprite"))
		{
			const auto material = compile_sprite(render.at("sprite"), catalog, path + ".render.sprite");
			if (material.frames.size() != (mode == "eight" ? 8u : 1u))
				invalid(path + ".render.sprite.frames", "frame count does not match billboard mode");
			if (render.contains("engines"))
			{
				const auto &engines = render.at("engines");
				const auto effect = compile_sprite(engines.at("sprite"), catalog, path + ".render.engines.sprite");
				if (effect.frames.size() != material.frames.size())
					invalid(path + ".render.engines", "thrust views must match parent views");
				for (const auto &frame : material.frames)
					if (std::none_of(effect.frames.begin(), effect.frames.end(),
					                 [&](const auto &f)
					                 {
						                 return f.name == frame.name;
					                 }))
						invalid(path + ".render.engines", "missing named thrust view");
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
				static const std::uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
				if (bytes.size() < 24 || !std::equal(std::begin(signature), std::end(signature), bytes.begin()))
					invalid(path + ".render.images", "expected PNG asset");
				int width = 0, height = 0, channels = 0;
				if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) ||
				    width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
				    static_cast<std::int64_t>(width) * height > 16777216)
					invalid(path + ".render.images", "invalid or oversized PNG dimensions");
				auto *decoded =
				    stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
				if (!decoded)
					invalid(path + ".render.images", "PNG cannot decode");
				stbi_image_free(decoded);
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
			if (it.key() == "engine")
			{
				integer(system, "max_speed_mm_s", field, 0);
				if (!catalog.presentation)
					integer(system, "acceleration_mm_s2", field, 1);
			}
			else if (it.key() == "steering")
			{
				if (!catalog.presentation)
					integer(system, "turn_mdeg_s", field, 1, 360000);
			}
			else if (it.key() == "radar")
				integer(system, "range_mm", field, 1);
			else
				invalid(field, "unsupported System in foundation vocabulary");
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
		if (entity.contains("controllers"))
		{
			if (!entity.contains("stations") || !entity["controllers"].is_object())
				invalid(path + ".controllers", "per-System controllers require explicit Stations");
			for (auto it = entity["controllers"].begin(); it != entity["controllers"].end(); ++it)
			{
				if (!entity["systems"].contains(it.key()) || !it.value().is_string() ||
				    !catalog.trees.contains(it.value().get<std::string>()))
					invalid(path + ".controllers." + it.key(), "unresolved System or tree");
				for (const auto &state : catalog.trees.at(it.value().get<std::string>()).at("states"))
					for (const auto &spec : operating_actions)
						if (state.contains(std::string(spec.name)))
						{
							if (operating_system_name(spec.system) != it.key())
								invalid(path + ".controllers." + it.key(), "tree writes another System");
							if (spec.action == OperatingAction::Speed &&
							    state.at(std::string(spec.name)).get<std::int64_t>() >
							        entity["systems"]["engine"]["max_speed_mm_s"].get<std::int64_t>())
								invalid(path + ".controllers." + it.key(), "speed output exceeds engine capability");
						}
			}
		}
		if (entity.contains("controller"))
		{
			const auto &systems = entity.at("systems");
			OperatingCapabilities capabilities;
			if (systems.contains("engine"))
				capabilities.engine_max_speed = systems.at("engine").at("max_speed_mm_s").get<std::int64_t>();
			capabilities.steering = systems.contains("steering");
			capabilities.radar = systems.contains("radar");
			for (const auto &state : catalog.trees.at(entity["controller"].get<std::string>()).at("states"))
			{
				for (const auto &output : operating_actions)
				{
					const std::string name(output.name);
					if (!state.contains(name))
						continue;
					if (!capabilities.contains(output.system))
						invalid(path + ".controller",
						        name + " output requires " + std::string(operating_system_name(output.system)));
					const auto value = output.boolean ? static_cast<std::int64_t>(state[name].get<bool>())
					                                  : state[name].get<std::int64_t>();
					if (!operating_permitted(output.action, value, capabilities))
						invalid(path + ".controller", "speed output exceeds the engine capability");
				}
			}
		}
	}
	for (const auto &[id, console] : catalog.consoles)
	{
		auto path = "consoles." + id;
		if (!console.contains("widgets") || !console["widgets"].is_array())
			invalid(path + ".widgets", "expected array");
		std::set<std::string> widget_ids;
		for (const auto &widget : console["widgets"])
		{
			auto widget_id = text(widget, "id", path);
			if (!widget_ids.insert(widget_id).second)
				invalid(path + "." + widget_id, "duplicate widget id");
			auto kind = text(widget, "kind", path + "." + widget_id);
			auto binding = text(widget, "binding", path + "." + widget_id);
			const auto bindings = widget_bindings();
			if (std::none_of(bindings.begin(), bindings.end(),
			                 [&](const auto &entry)
			                 {
				                 return kind == entry.kind && binding == entry.binding;
			                 }))
				invalid(path + "." + widget_id, "unsupported widget/binding combination");
			text(widget, "label", path + "." + widget_id);
			auto x = integer(widget, "x", path, 0, 1000), y = integer(widget, "y", path, 0, 1000);
			auto w = integer(widget, "width", path, 1, 1000), h = integer(widget, "height", path, 1, 1000);
			if (x + w > 1000 || y + h > 1000)
				invalid(path + "." + widget_id, "widget exceeds the 1000 by 1000 authored layout");
		}
	}
	for (const auto &[id, map] : catalog.maps)
	{
		if (map.contains("lighting"))
			verify_lighting(map.at("lighting"), "maps." + id + ".lighting");
		auto path = "maps." + id;
		if (!map.contains("entities") || !map["entities"].is_array() || !map.contains("ship_slots") ||
		    !map["ship_slots"].is_array())
			invalid(path, "entities and ship_slots arrays required");
		std::set<std::int64_t> entity_ids;
		std::set<std::string> slots;
		for (const auto &instance : map["entities"])
		{
			auto entity_id = integer(instance, "id", path, 1);
			if (!entity_ids.insert(entity_id).second)
				invalid(path, "duplicate entity instance id");
			auto definition = text(instance, "definition", path);
			if (!catalog.entities.contains(definition))
				invalid(path, "unknown entity definition '" + definition + "'");
			transform(instance, path + ".entity." + std::to_string(entity_id));
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
			std::set<std::string> allowed;
			for (const auto &ship : slot["allowed_ships"])
			{
				if (!ship.is_string() || !catalog.entities.contains(ship.get<std::string>()) ||
				    !allowed.insert(ship.get<std::string>()).second)
					invalid(path + ".slot." + slot_id, "unknown or repeated allowed ship");
				const auto &definition = catalog.entities.at(ship.get<std::string>());
				if (!definition["systems"].contains("engine") || !definition["systems"].contains("steering") ||
				    definition["collision"]["mass_grams"].get<std::int64_t>() <= 0)
					invalid(path + ".slot." + slot_id, "allowed ship needs engine, steering and positive mass");
			}
		}
	}
}
} // namespace phoenix
