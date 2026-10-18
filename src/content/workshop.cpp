module;
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

module phoenix.content;

namespace phoenix
{
WorkshopDocument::WorkshopDocument()
{
	reset();
}
void WorkshopDocument::reset()
{
	pack_ = PackDraft{};
	pack_.manifest = {{"schema", 1}, {"id", "workshop"}};
	libraries_.clear();
	library_catalog_ = Catalog{};
	definitions_ = {{"schema", 1},
	                {"entities", Json::array()},
	                {"maps", Json::array()},
	                {"consoles", Json::array()},
	                {"trees", Json::array()}};
	new_map("workshop.map");
}
void WorkshopDocument::new_map(const std::string &id)
{
	for (const auto &map : definitions_["maps"])
		if (map["id"] == id)
			throw std::runtime_error("Map id already exists: " + id);
	definitions_["maps"].push_back({{"id", id}, {"entities", Json::array()}, {"ship_slots", Json::array()}});
	map_index_ = definitions_["maps"].size() - 1;
}
void WorkshopDocument::select_map(std::size_t index)
{
	if (index >= definitions_["maps"].size())
		throw std::runtime_error("Unknown map index");
	map_index_ = index;
}
void WorkshopDocument::load_pack(std::span<const std::uint8_t> archive)
{
	auto loaded = read_pack(archive);
	auto order = libraries_;
	order.push_back(loaded);
	auto resolved = resolve_packs(order);
	if (resolved.presentation)
		throw std::runtime_error("Console assets are not an editable gameplay pack");
	auto definitions = parse_json(loaded.files().at("definitions.json"), "definitions.json");
	for (auto kind : {"entities", "maps", "consoles", "trees"})
		if (!definitions.contains(kind))
			definitions[kind] = Json::array();
	auto flatten = [&](const char *kind, const auto &table)
	{
		for (auto &value : definitions[kind])
			value = table.at(value.at("id").template get<std::string>());
	};
	flatten("entities", resolved.entities);
	flatten("maps", resolved.maps);
	flatten("consoles", resolved.consoles);
	flatten("trees", resolved.trees);
	if (definitions["maps"].empty())
		definitions["maps"].push_back(
		    {{"id", "workshop.map"}, {"entities", Json::array()}, {"ship_slots", Json::array()}});
	pack_ = loaded.draft();
	definitions_ = std::move(definitions);
	map_index_ = 0;
}
void WorkshopDocument::load_dependency(std::span<const std::uint8_t> archive)
{
	auto library = read_pack(archive);
	auto order = libraries_;
	order.push_back(library);
	auto catalog = resolve_packs(order);
	if (catalog.presentation)
		throw std::runtime_error("Console assets are not a gameplay dependency");
	auto id = library.manifest().at("id").get<std::string>();
	if (id == pack_.manifest.at("id").get<std::string>())
		throw std::runtime_error("Rename the authored pack before loading a dependency with the same id");
	libraries_ = std::move(order);
	library_catalog_ = std::move(catalog);
	if (!pack_.manifest.contains("dependencies"))
		pack_.manifest["dependencies"] = Json::array();
	auto &dependencies = pack_.manifest["dependencies"];
	if (std::find(dependencies.begin(), dependencies.end(), Json(id)) == dependencies.end())
		dependencies.push_back(id);
}
void WorkshopDocument::rename_pack(const std::string &id)
{
	if (id.empty() || id.size() > 128 ||
	    id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos)
		throw std::runtime_error("Pack id uses lower-case letters, numbers, dots, underscores and hyphens");
	for (const auto &[loaded, hash] : library_catalog_.ordered_packs)
		if (loaded == id)
			throw std::runtime_error("Pack id already belongs to a dependency");
	auto old = pack_.manifest.at("id").get<std::string>();
	std::function<void(Json &)> rename = [&](Json &value)
	{
		if (value.is_string())
		{
			auto text = value.get<std::string>();
			if (text.starts_with(old + ".") || text.starts_with(old + ":"))
				value = id + text.substr(old.size());
		}
		else if (value.is_object())
		{
			for (auto it = value.begin(); it != value.end(); ++it)
				if (it.key() != "label")
					rename(it.value());
		}
		else if (value.is_array())
			for (auto &child : value)
				rename(child);
	};
	rename(definitions_);
	pack_.manifest["id"] = id;
}
Catalog WorkshopDocument::verify() const
{
	PackDraft draft;
	draft.manifest = pack_.manifest;
	draft.files = pack_.files;
	draft.files["definitions.json"] = json_bytes(definitions_);
	return resolve_draft(libraries_, draft);
}
Bytes WorkshopDocument::export_pack() const
{
	PackDraft draft;
	draft.manifest = pack_.manifest;
	draft.files = pack_.files;
	draft.files["definitions.json"] = json_bytes(definitions_);
	draft.files["launcher.json"] = json_bytes(launcher_catalog(resolve_draft(libraries_, draft)));
	return write_pack(draft);
}
void WorkshopDocument::load_map(std::span<const std::uint8_t> bytes)
{
	auto value = parse_json(bytes, "map.json");
	auto candidate = *this;
	bool replaced = false;
	for (std::size_t i = 0; i < candidate.definitions_["maps"].size(); ++i)
	{
		if (candidate.definitions_["maps"][i]["id"] == value.value("id", std::string{}))
		{
			candidate.definitions_["maps"][i] = value;
			candidate.map_index_ = i;
			replaced = true;
			break;
		}
	}
	if (!replaced)
	{
		candidate.definitions_["maps"].push_back(value);
		candidate.map_index_ = candidate.definitions_["maps"].size() - 1;
	}
	candidate.verify();
	*this = std::move(candidate);
}
Bytes WorkshopDocument::save_map() const
{
	verify();
	return json_bytes(map());
}
std::string WorkshopDocument::create_entity(const std::string &parent)
{
	std::size_t number = definitions_["entities"].size() + 1;
	std::string id;
	do
	{
		id = pack_.manifest.at("id").get<std::string>() + ".entity" + std::to_string(number++);
	} while (std::any_of(definitions_["entities"].begin(), definitions_["entities"].end(),
	                     [&](const auto &entity)
	                     {
		                     return entity["id"] == id;
	                     }));
	if (!parent.empty())
	{
		if (!library_catalog_.entities.contains(parent))
			throw std::runtime_error("Unknown library entity: " + parent);
		definitions_["entities"].push_back({{"id", id}, {"extends", parent}});
		return id;
	}
	std::string image;
	for (const auto &[path, bytes] : pack_.files)
		if (path.starts_with("assets/"))
		{
			image = pack_.manifest.at("id").get<std::string>() + ":" + path;
			break;
		}
	if (image.empty() && !library_catalog_.assets.empty())
		image = library_catalog_.assets.begin()->first;
	definitions_["entities"].push_back(
	    {{"id", id},
	     {"render", {{"mode", "camera"}, {"images", Json::array({image})}, {"scale", 1}}},
	     {"systems",
	      {{"engine", {{"max_speed_mm_s", 30000}, {"acceleration_mm_s2", 3000}}},
	       {"steering", {{"turn_mdeg_s", 45000}}},
	       {"radar", {{"range_mm", 200000}}}}},
	     {"collision", {{"shape", "circle"}, {"radius_mm", 2000}, {"mass_grams", 1000000}}}});
	return id;
}
std::uint64_t WorkshopDocument::place(const std::string &definition, std::int64_t x, std::int64_t y)
{
	bool exists = std::any_of(definitions_["entities"].begin(), definitions_["entities"].end(),
	                          [&](const auto &entity)
	                          {
		                          return entity["id"] == definition;
	                          });
	if (!exists && !library_catalog_.entities.contains(definition))
		throw std::runtime_error("Cannot place unknown definition: " + definition);
	std::uint64_t id = 1;
	for (const auto &entity : map()["entities"])
		id = std::max(id, entity.at("id").get<std::uint64_t>() + 1);
	map()["entities"].push_back(
	    {{"id", id}, {"definition", definition}, {"x_mm", x}, {"y_mm", y}, {"heading_mdeg", 0}});
	return id;
}
void WorkshopDocument::erase(std::uint64_t instance)
{
	auto &entities = map()["entities"];
	auto found = std::find_if(entities.begin(), entities.end(),
	                          [&](const auto &entity)
	                          {
		                          return entity["id"] == instance;
	                          });
	if (found != entities.end())
		entities.erase(found);
}
void WorkshopDocument::duplicate(std::uint64_t instance)
{
	for (const auto &entity : map()["entities"])
		if (entity["id"] == instance)
		{
			auto copy = entity;
			auto offset = [](std::int64_t position)
			{
				return std::clamp(position, static_cast<std::int64_t>(-1000000000),
				                  static_cast<std::int64_t>(999995000)) +
				       5000;
			};
			auto id = place(copy["definition"], offset(copy["x_mm"].get<std::int64_t>()),
			                offset(copy["y_mm"].get<std::int64_t>()));
			map()["entities"].back()["heading_mdeg"] = copy["heading_mdeg"];
			(void)id;
			return;
		}
}
std::string WorkshopDocument::import_image(const std::string &filename, Bytes bytes)
{
	if (bytes.size() > 16 * 1024 * 1024)
		throw std::runtime_error("Image exceeds 16 MiB");
	auto leaf =
	    filename.substr(filename.find_last_of("/\\") == std::string::npos ? 0 : filename.find_last_of("/\\") + 1);
	if (leaf.empty() || leaf == "." || leaf == ".." || leaf.find(':') != std::string::npos)
		throw std::runtime_error("Invalid image filename");
	auto path = "assets/" + leaf;
	if (pack_.files.contains(path))
		throw std::runtime_error("Asset already exists; rename the imported file: " + path);
	// Verify using the same image contract as referenced entity render assets.
	Catalog probe;
	auto reference = pack_.manifest.at("id").get<std::string>() + ":" + path;
	probe.assets[reference] = bytes;
	probe.entities["import"] = {{"render", {{"mode", "camera"}, {"images", Json::array({reference})}, {"scale", 1}}},
	                            {"systems", Json::object()},
	                            {"collision", {{"shape", "circle"}, {"radius_mm", 1}, {"mass_grams", 0}}}};
	verify_catalog(probe);
	pack_.files[path] = std::move(bytes);
	return reference;
}
} // namespace phoenix
