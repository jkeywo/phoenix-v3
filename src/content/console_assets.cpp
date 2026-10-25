module;
#include <algorithm>
#include <functional>
#include <tuple>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <stdexcept>
#include <vector>

module phoenix.content;
namespace phoenix
{
namespace
{
Json public_ui_definition(const Json &source)
{
	if (source.is_array())
	{
		Json result = Json::array();
		for (const auto &value : source)
			result.push_back(public_ui_definition(value));
		return result;
	}
	if (!source.is_object())
		return source;
	Json result = Json::object();
	static const std::set<std::string> fields = {
	    "id",   "schema",  "root",   "compact", "kind",   "label", "component", "group",     "overrides",  "children",
	    "rect", "circle",  "x",      "y",       "radius", "width", "height",    "min_width", "min_height", "stretch",
	    "gap",  "padding", "aspect", "align",   "region", "style", "tokens",    "assets",    "slices",     "when"};
	for (const auto &[key, value] : source.items())
		if (fields.contains(key))
			result[key] = key == "tokens" || key == "assets" || key == "slices" || key == "style" || key == "when"
			                  ? value
			                  : public_ui_definition(value);
	return result;
}
PackDraft console_assets_draft(const Catalog &catalog, const std::string &ship,
                               const std::vector<std::string> &console_ids, const std::string &background)
{
	const auto &source = catalog.entities.at(ship);
	PackDraft draft;
	const std::string pack_id = "console-assets";
	draft.manifest = {{"schema", 1}, {"id", pack_id}, {"format", "phoenix-console-assets"}};
	if (!catalog.base.id.empty())
		draft.manifest["presentation_base"] = {{"id", catalog.base.id},
		                                       {"hash", catalog.base.hash},
		                                       {"render_mode", catalog.base.render_mode},
		                                       {"interface_style", catalog.base.interface_style}};
	if (console_ids == std::vector<std::string>{"__selection__"})
	{
		draft.manifest["format"] = "phoenix-console-selection";
		draft.files["definitions.json"] = json_bytes(Json{{"schema", 3}});
		return draft;
	}
	const bool contact_only = source.at("render").contains("model") || source.at("render").contains("star");
	Json entity{{"id", ship}, {"systems", Json::object()}};
	entity["render"] = {{"mode", source.at("render").value("mode", std::string("camera"))},
	                    {"scale", source.at("render").at("scale")},
	                    {"images", Json::array()}};
	if (!source.at("render").contains("sprite") && source.at("render").contains("radar_icon"))
		entity["render"]["radar_icon"] = std::string(radar_icon_name(render_radar_icon(source.at("render"))));
	auto copy_image = [&](const Json &image)
	{
		const auto &bytes = catalog.assets.at(image.get<std::string>());
		const auto path = "assets/" + sha256(bytes) + ".png";
		draft.files[path] = bytes;
		return pack_id + ":" + path;
	};
	// Radar PNGs are public presentation assets. Include these without exposing
	// other hulls, maps, systems, controllers or arbitrary authored metadata.
	for (const auto &[id, definition] : catalog.entities)
	{
		const auto reference = radar_sprite_reference(definition.at("render"));
		if (!reference.empty())
		{
			const auto &png = catalog.assets.at(reference);
			const auto address = radar_sprite_id(png);
			draft.files[address.substr(pack_id.size() + 1)] = png;
		}
	}
	for (const auto &[name, bytes] : catalog.assets)
		if (name.starts_with("console-assets:assets/comms/"))
			draft.files[name.substr(pack_id.size() + 1)] = bytes;
	const auto own_icon = render_radar_sprite(source.at("render"), catalog);
	if (!own_icon.empty() && !source.at("render").contains("sprite"))
		entity["render"]["radar_sprite"] = own_icon;
	if (contact_only)
	{
		entity["render"].erase("mode");
		entity["render"].erase("images");
		entity["render"]["contact_only"] = true;
	}
	else if (source.at("render").contains("sprite"))
	{
		auto copy_material = [&](const Json &original)
		{
			Json material = {{"albedo", copy_image(original.at("albedo"))}, {"frames", Json::array()}};
			for (const char *field : {"normal", "depth"})
				if (original.contains(field) && !original.at(field).is_null())
					material[field] = copy_image(original.at(field));
			material["emissive"] = original.value("emissive", false);
			if (original.contains("radar_sprite") && !original.at("radar_sprite").get<std::string>().empty())
				material["radar_sprite"] = radar_sprite_id(catalog.assets.at(original.at("radar_sprite")));
			if (original.contains("radar_icon"))
				material["radar_icon"] =
				    std::string(radar_icon_name(compile_radar_icon(original.at("radar_icon"), "radar_icon")));
			auto copy_frames = [&](const Json &frames)
			{
				Json copied = Json::array();
				for (const auto &f : frames)
				{
					Json frame;
					for (const char *field :
					     {"name", "rect", "pivot", "forward", "mirror_x", "azimuth", "depth_scale", "depth_offset"})
						if (f.contains(field))
							frame[field] = f.at(field);
					frame["attachments"] = Json::object();
					const auto attachments = f.value("attachments", Json::object());
					for (auto it = attachments.begin(); it != attachments.end(); ++it)
						for (const char *field : {"x", "y", "layer", "depth_offset"})
							if (it.value().contains(field))
								frame["attachments"][it.key()][field] = it.value().at(field);
					copied.push_back(std::move(frame));
				}
				return copied;
			};
			if (original.contains("entries"))
			{
				material.erase("frames");
				material["entries"] = Json::array();
				for (const auto &entry : original.at("entries"))
					material["entries"].push_back(
					    {{"name", entry.at("name")}, {"frames", copy_frames(entry.at("frames"))}});
			}
			else
				material["frames"] = copy_frames(original.at("frames"));
			return material;
		};
		entity["render"].erase("images");
		entity["render"]["sprite"] = copy_material(source.at("render").at("sprite"));
		if (source.at("render").contains("sprite_entry"))
			entity["render"]["sprite_entry"] = source.at("render").at("sprite_entry");
		if (source.at("render").contains("engines"))
		{
			const auto &engines = source.at("render").at("engines");
			entity["render"]["engines"] = {{"sprite", copy_material(engines.at("sprite"))}};
			for (const char *field : {"scale", "range", "power"})
				entity["render"]["engines"][field] = engines.at(field);
		}
	}
	else
		for (const auto &image : source.at("render").at("images"))
			entity["render"]["images"].push_back(copy_image(image));
	const auto &systems = source.at("systems");
	for (const auto &system : resolve_systems(catalog, ship))
	{
		auto &copy = entity["systems"][system.id] = Json::object();
		if (systems.at(system.id).contains("kind"))
			copy["kind"] = std::string(operating_system_name(system.kind));
		if (system.kind == OperatingSystem::Engine || system.kind == OperatingSystem::Lateral ||
		    system.kind == OperatingSystem::Boost || system.kind == OperatingSystem::Impulse)
			copy["max_speed_mm_s"] = system.maximum;
		else if (system.kind == OperatingSystem::Radar || system.kind == OperatingSystem::Comms)
		{
			copy["range_mm"] = system.maximum;
			if (systems.at(system.id).contains("shows"))
				copy["shows"] = systems.at(system.id).at("shows");
		}
		else
			copy["maximum"] = system.maximum;
		if (system.kind == OperatingSystem::Displacement)
			for (const auto *field : {"range_mm", "capacity", "transfer_ticks"})
				copy[field] = systems.at(system.id).at(field);
	}
	Json consoles = Json::array(), stations = Json::array();
	std::set<std::string> included;
	for (const auto &seat : resolve_consoles(catalog, ship))
	{
		if (!console_ids.empty() && std::find(console_ids.begin(), console_ids.end(), seat.id) == console_ids.end())
			continue;
		Json controls = seat.controlled_systems, feeds = seat.feed_systems;
		// Copy the public fields, never arbitrary authored metadata.
		auto station = std::find_if(stations.begin(), stations.end(),
		                            [&](const Json &value)
		                            {
			                            return value.at("id") == seat.station;
		                            });
		if (station == stations.end())
		{
			stations.push_back({{"id", seat.station}, {"consoles", Json::array()}});
			station = stations.end() - 1;
		}
		(*station)["consoles"].push_back({{"id", seat.id},
		                                  {"definition", seat.definition},
		                                  {"systems", controls},
		                                  {"feeds", feeds},
		                                  {"shield_feed", seat.shield_feed},
		                                  {"human_preferences", seat.human_preferences}});
		if (included.insert(seat.definition).second)
		{
			Json layout{{"id", seat.definition}, {"widgets", Json::array()}};
			for (const char *field :
			     {"name", "description", "human_seeking", "stances", "default_stance", "command", "hide_without_ai",
			      "viewscreen", "compact", "hud_layout", "hud_layout_overrides", "layout", "layout_overrides", "theme",
			      "theme_overrides", "gameplay_panel", "when", "tab"})
				if (catalog.consoles.at(seat.definition).contains(field))
					layout[field] = catalog.consoles.at(seat.definition).at(field);
			for (const auto &widget : catalog.consoles.at(seat.definition).at("widgets"))
			{
				Json copy;
				for (const auto *field : {"id", "kind", "label", "x", "y", "width", "height"})
					copy[field] = widget.at(field);
				if (widget.contains("binding"))
					copy["binding"] = widget.at("binding");
				if (widget.contains("system"))
					copy["system"] = widget.at("system");
				if (widget.contains("sweep"))
					copy["sweep"] = widget.at("sweep");
				for (const auto *field : {"controller", "channel", "when"})
					if (widget.contains(field))
						copy[field] = widget.at(field);
				layout["widgets"].push_back(std::move(copy));
			}
			consoles.push_back(std::move(layout));
		}
	}
	// A restricted Console bundle contains only admitted Stations. Preferences
	// resolved against the full ship may point at seats absent from this projection.
	std::set<std::string> projected_stations;
	for (const auto &station : stations)
		projected_stations.insert(station.at("id").get<std::string>());
	for (auto &station : stations)
		for (auto &console : station["consoles"])
		{
			auto preferences = console["human_preferences"].get<std::vector<std::string>>();
			std::erase_if(preferences,
			              [&](const std::string &id)
			              {
				              return !projected_stations.contains(id);
			              });
			console["human_preferences"] = std::move(preferences);
		}
	entity["stations"] = stations;
	if (!console_ids.empty())
	{
		// A Console needs public radar icons, not the ship's detailed hull material.
		entity["render"] = {{"mode", "camera"}, {"scale", 1}, {"images", Json::array()}};
		const AssetImage pixel{1, 1, {255, 255, 255, 255}};
		if (!own_icon.empty())
			entity["render"]["radar_sprite"] = own_icon;
		entity["render"]["radar_icon"] = std::string(radar_icon_name(render_radar_icon(source.at("render"))));
		if (contact_only)
		{
			entity["render"].erase("mode");
			entity["render"].erase("images");
			entity["render"]["contact_only"] = true;
		}
		else
		{
			draft.files["assets/contact.png"] = source_asset_image(pixel);
			entity["render"]["images"].push_back(pack_id + ":assets/contact.png");
		}
		std::erase_if(draft.files,
		              [](const auto &entry)
		              {
			              return entry.first.starts_with("assets/") && entry.first != "assets/contact.png" &&
			                     !entry.first.starts_with("assets/radar/") && !entry.first.starts_with("assets/comms/");
		              });
	}
	const auto loading = background.empty() ? catalog.loading_background : background;
	if (!loading.empty())
	{
		draft.files["assets/loading.png"] = catalog.assets.at(loading);
		draft.manifest["loading_background"] = pack_id + ":assets/loading.png";
	}
	Json definitions{{"schema", 3}, {"entities", Json::array({entity})}, {"consoles", consoles}};
	// Only definitions reachable from the admitted Console presentation are transferred.
	std::set<std::string> layouts, groups, themes;
	if (!catalog.ui_theme.empty())
	{
		themes.insert(catalog.ui_theme);
		draft.manifest["theme"] = catalog.ui_theme;
	}
	std::function<void(const Json &)> collect = [&](const Json &node)
	{
		if (node.is_object())
		{
			if (node.contains("group") && node.at("group").is_string())
			{
				const auto id = node.at("group").get<std::string>();
				if (groups.insert(id).second)
					collect(catalog.ui_groups.at(id));
			}
			for (const char *key : {"root", "compact", "children", "overrides"})
				if (node.contains(key))
					collect(node.at(key));
		}
		else if (node.is_array())
			for (const auto &value : node)
				collect(value);
	};
	for (const auto &console : consoles)
	{
		if (console.contains("layout"))
		{
			if (console.at("layout").is_string())
			{
				const auto id = console.at("layout").get<std::string>();
				layouts.insert(id);
				collect(catalog.layouts.at(id));
			}
			else
				collect(console.at("layout"));
		}
		if (console.contains("hud_layout"))
		{
			const auto &hud = console.at("hud_layout");
			if (hud.is_string())
			{
				const auto id = hud.get<std::string>();
				layouts.insert(id);
				collect(catalog.layouts.at(id));
			}
			else
				collect(hud);
		}
		if (console.contains("theme"))
			themes.insert(console.at("theme").get<std::string>());
		if (console.contains("layout_overrides"))
			collect(public_ui_definition(console.at("layout_overrides")));
		if (console.contains("hud_layout_overrides"))
			collect(public_ui_definition(console.at("hud_layout_overrides")));
	}
	for (const auto &[ids, table, name] :
	     {std::tuple{layouts, catalog.layouts, "layouts"}, std::tuple{groups, catalog.ui_groups, "ui_groups"},
	      std::tuple{themes, catalog.themes, "themes"}})
	{
		definitions[name] = Json::array();
		for (const auto &id : ids)
		{
			auto definition = public_ui_definition(table.at(id));
			definition.erase("extends");
			if (std::string(name) == "themes" && definition.contains("assets"))
				for (auto &asset : definition["assets"].items())
				{
					const auto ref = asset.value().get<std::string>();
					const auto dot = ref.find_last_of('.');
					const auto path = "assets/theme/" + sha256(catalog.assets.at(ref)) +
					                  (dot == std::string::npos ? "" : ref.substr(dot));
					draft.files[path] = catalog.assets.at(ref);
					asset.value() = pack_id + ":" + path;
				}
			definitions[name].push_back(std::move(definition));
		}
	}
	for (auto &console : definitions["consoles"])
	{
		for (const char *field : {"layout", "hud_layout", "layout_overrides", "hud_layout_overrides"})
			if (console.contains(field))
				console[field] = public_ui_definition(console.at(field));
		if (console.contains("theme_overrides") && console.at("theme_overrides").contains("assets"))
			for (auto &asset : console["theme_overrides"]["assets"].items())
			{
				const auto ref = asset.value().get<std::string>();
				const auto dot = ref.find_last_of('.');
				const auto path = "assets/theme/" + sha256(catalog.assets.at(ref)) +
				                  (dot == std::string::npos ? "" : ref.substr(dot));
				draft.files[path] = catalog.assets.at(ref);
				asset.value() = pack_id + ":" + path;
			}
	}
	draft.files["definitions.json"] = json_bytes(definitions);
	return draft;
}
} // namespace
Pack console_assets(const Catalog &catalog, const std::string &ship, const std::vector<std::string> &console_ids,
                    const std::string &background)
{
	auto pack = read_pack(write_pack(console_assets_draft(catalog, ship, console_ids, background),
	                                 console_ids.empty() ? PackEncoding::Zip : PackEncoding::Dat));
	resolve_packs({pack});
	return pack;
}
Pack console_assets(const Catalog &catalog, const std::string &ship, const std::string &console,
                    const std::string &background)
{
	return console_assets(catalog, ship,
	                      console.empty() ? std::vector<std::string>{} : std::vector<std::string>{console}, background);
}
Pack ship_client_assets(const Catalog &catalog, const std::string &ship, const std::string &background)
{
	std::vector<std::string> consoles;
	for (const auto &seat : resolve_consoles(catalog, ship))
		consoles.push_back(seat.id);
	if (consoles.empty())
		throw std::runtime_error("Ship has no player Consoles");
	auto draft = console_assets_draft(catalog, ship, consoles, background);
	draft.manifest["source_packs"] = catalog.ordered_packs;
	draft.manifest["ship"] = ship;
	auto pack = read_pack(write_pack(draft, PackEncoding::Dat));
	resolve_packs({pack});
	return pack;
}
void validate_ship_client(const Pack &pack, const Catalog &catalog, const Json &descriptor)
{
	const auto &manifest = pack.manifest();
	if (pack.hash() != descriptor.at("hash").get<std::string>() ||
	    manifest.value("format", std::string{}) != "phoenix-console-assets" ||
	    manifest.at("ship") != descriptor.at("ship") || manifest.at("source_packs") != descriptor.at("sources"))
		throw std::runtime_error("Ship presentation does not match the host content");
	if (!catalog.presentation || !catalog.maps.empty() || !catalog.trees.empty() || catalog.entities.size() != 1 ||
	    !catalog.entities.contains(descriptor.at("ship").get<std::string>()))
		throw std::runtime_error("Invalid ship presentation content");
}
void validate_ship_client(const Pack &pack, const Json &descriptor)
{
	validate_ship_client(pack, resolve_packs({pack}), descriptor);
}
} // namespace phoenix
