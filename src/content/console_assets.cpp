module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.content;
namespace phoenix
{
Pack console_assets(const Catalog &catalog, const std::string &ship, const std::string &console,
                    const std::string &background)
{
	const auto &source = catalog.entities.at(ship);
	PackDraft draft;
	const std::string pack_id = "console-assets";
	draft.manifest = {{"schema", 1}, {"id", pack_id}, {"format", "phoenix-console-assets"}};
	if (console == "__selection__")
	{
		draft.manifest["format"] = "phoenix-console-selection";
		draft.files["definitions.json"] = json_bytes(Json{{"schema", 3}});
		return read_pack(write_pack(draft, PackEncoding::Dat));
	}
	Json entity{{"id", ship}, {"systems", Json::object()}};
	entity["render"] = {{"mode", source.at("render").at("mode")},
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
	const auto own_icon = render_radar_sprite(source.at("render"), catalog);
	if (!own_icon.empty() && !source.at("render").contains("sprite"))
		entity["render"]["radar_sprite"] = own_icon;
	if (source.at("render").contains("sprite"))
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
					for (const char *field : {"name", "rect", "pivot", "azimuth", "depth_scale", "depth_offset"})
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
		else if (system.kind == OperatingSystem::Radar)
		{
			copy["range_mm"] = system.maximum;
			if (systems.at(system.id).contains("shows"))
				copy["shows"] = systems.at(system.id).at("shows");
		}
		else
			copy["maximum"] = system.maximum;
	}
	Json consoles = Json::array(), stations = Json::array();
	std::set<std::string> included;
	for (const auto &seat : resolve_consoles(catalog, ship))
	{
		if (!console.empty() && seat.id != console)
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
		                                  {"human_preferences", seat.human_preferences}});
		if (included.insert(seat.definition).second)
		{
			Json layout{{"id", seat.definition}, {"widgets", Json::array()}};
			for (const char *field : {"name", "description", "human_seeking"})
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
				for (const auto *field : {"controller", "channel"})
					if (widget.contains(field))
						copy[field] = widget.at(field);
				layout["widgets"].push_back(std::move(copy));
			}
			consoles.push_back(std::move(layout));
		}
	}
	entity["stations"] = stations;
	if (!console.empty())
	{
		// A Console needs public radar icons, not the ship's detailed hull material.
		entity["render"] = {{"mode", "camera"}, {"scale", 1}, {"images", Json::array()}};
		const Bytes pixel = {'P', 'H', 'X', 'R', 'G', 'B', 'A', '1', 1, 0, 0, 0, 1, 0, 0, 0, 255, 255, 255, 255};
		if (!own_icon.empty())
			entity["render"]["radar_sprite"] = own_icon;
		entity["render"]["radar_icon"] = std::string(radar_icon_name(render_radar_icon(source.at("render"))));
		draft.files["assets/contact.png"] = source_asset_image(pixel);
		entity["render"]["images"].push_back(pack_id + ":assets/contact.png");
		std::erase_if(draft.files,
		              [](const auto &entry)
		              {
			              return entry.first.starts_with("assets/") && entry.first != "assets/contact.png" &&
			                     !entry.first.starts_with("assets/radar/");
		              });
	}
	const auto loading = background.empty() ? catalog.loading_background : background;
	if (!loading.empty())
	{
		draft.files["assets/loading.png"] = catalog.assets.at(loading);
		draft.manifest["loading_background"] = pack_id + ":assets/loading.png";
	}
	draft.files["definitions.json"] =
	    json_bytes(Json{{"schema", 3}, {"entities", Json::array({entity})}, {"consoles", consoles}});
	auto pack = read_pack(write_pack(draft, console.empty() ? PackEncoding::Zip : PackEncoding::Dat));
	resolve_packs({pack});
	return pack;
}
} // namespace phoenix
