module;
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.workshop.workflow;
import phoenix.content.documents;
import phoenix.content.packs;
import phoenix.workshop.map;
import phoenix.workshop.layout;
import phoenix.workshop.asset_preview;
import phoenix.workshop.material_preview;
import phoenix.workshop.gameplay;
import phoenix.ui.preview;

namespace phoenix
{
#include "state.hpp"
void WorkshopWorkspace::State::save_pack(WorkshopEnvironment &environment, bool save_as)
{
	if (saving)
		return;
	const auto bytes =
	    document.export_pack(pack_location.filename.ends_with(".dat") ? PackEncoding::Dat : PackEncoding::Zip);
	if (!save_as && !library_pack.empty())
	{
		if (!environment.library || environment.active)
			throw std::runtime_error("Leave the active game before replacing an installed pack");
		const auto saved = environment.library->replace_pack(library_pack, bytes);
		library_pack = saved.hash;
		if (environment.session)
			environment.session->replace_packs(environment.library->archives(environment.library->packs));
		saving_library = true;
		request_library_save(saves, saved.id + ".zip");
		saving = true;
		status = "Saving pack...";
		return;
	}
	const auto filename = pack_location.filename.empty() ? document.manifest().at("id").get<std::string>() + ".zip"
	                                                     : pack_location.filename;
	request_save(saves, FilePurpose::WorkshopPackSave, filename, bytes, pack_location.destination, save_as);
	saving = true;
	status = "Saving pack...";
}

std::pair<std::string, Bytes> WorkshopWorkspace::State::installed_base_pack(WorkshopEnvironment &environment)
{
	if (!environment.library)
		throw std::runtime_error("The installed pack library is unavailable");
	const auto entry_it = std::find_if(environment.library->packs.begin(), environment.library->packs.end(),
	                                   [&](const auto &entry)
	                                   {
		                                   return entry.hash == environment.library->base().hash;
	                                   });
	if (entry_it == environment.library->packs.end())
		throw std::runtime_error("The launch base is unavailable");
	const auto entry = *entry_it;
	return {entry.hash, environment.library->read_pack_archive(entry.hash)};
}

void WorkshopWorkspace::State::open_base_pack(WorkshopEnvironment &environment)
{
	const auto [hash, archive] = installed_base_pack(environment);
	WorkshopDocument candidate;
	candidate.load_pack(archive);
	document = std::move(candidate);
	reset_save_locations();
	library_pack = hash;
	clear_textures();
	pack_name = document.manifest().at("id").get<std::string>();
	selection.definition = 0;
	console = 0;
	selection.instance = 0;
	pending_sidecar = nullptr;
	status = "Editing the launch base. Export a separate file and relaunch to use changes.";
}

void WorkshopWorkspace::State::save_map(bool save_as)
{
	if (saving)
		return;
	const auto id = document.map().at("id").get<std::string>();
	const auto &location = map_locations[id];
	const auto bytes = document.save_map();
	request_save(saves, FilePurpose::WorkshopMapSave, location.filename.empty() ? id + ".json" : location.filename,
	             bytes, location.destination, save_as);
	saving_map = id;
	saving = true;
	status = "Saving map...";
}

void WorkshopWorkspace::State::add_map()
{
	add_workshop_map(document, selection, status);
}

void WorkshopWorkspace::State::pack_parent_properties(WorkshopEnvironment &environment)
{
	if (!environment.library || !ImGui::CollapsingHeader(tool_label("Mod tree").c_str()))
		return;
	std::string identity;
	for (const auto &pack : environment.library->packs)
		identity += pack.id + ":" + pack.hash + ";";
	if (identity != mod_tree_identity)
	{
		mod_manifests.clear();
		mod_hashes.clear();
		attempt(
		    [&]
		    {
			    for (const auto &entry : environment.library->packs)
			    {
				    auto archive = environment.library->read_pack_archive(entry.hash);
				    mod_manifests[entry.id] = read_pack(archive).manifest();
				    mod_hashes[entry.id] = entry.hash;
			    }
			    mod_tree_identity = identity;
		    });
	}
	const auto own = document.manifest().at("id").get<std::string>();
	const auto root = environment.library->base().id;
	auto path = [&](std::string id)
	{
		std::vector<std::string> chain;
		std::set<std::string> seen;
		while (seen.insert(id).second && mod_manifests.contains(id))
		{
			chain.push_back(id);
			if (id == root)
				break;
			id = mod_manifests.at(id).value("parent", std::string{});
		}
		return chain;
	};
	for (const auto &entry : environment.library->packs)
	{
		const auto chain = path(entry.id);
		const auto depth = chain.empty() ? 0 : chain.size() - 1;
		if (depth)
			ImGui::Indent(static_cast<float>(depth) * 16);
		ImGui::TextUnformatted(entry.id.c_str());
		if (depth)
			ImGui::Unindent(static_cast<float>(depth) * 16);
	}
	auto &manifest = document.manifest();
	if (manifest.contains("base"))
	{
		ImGui::TextUnformatted(localise("Base pack: parent is fixed").c_str());
		return;
	}
	const auto parent = manifest.value("parent", std::string{});
	if (ImGui::BeginCombo(tool_label("Parent mod").c_str(), parent.c_str()))
	{
		for (const auto &entry : environment.library->packs)
		{
			const auto chain = path(entry.id);
			if (chain.empty() || chain.back() != root || std::find(chain.begin(), chain.end(), own) != chain.end())
				continue;
			if (ImGui::Selectable(entry.id.c_str(), parent == entry.id))
				attempt(
				    [&]
				    {
					    auto candidate = document;
					    std::set<std::string> loaded;
					    for (const auto &[id, hash] : candidate.libraries().ordered_packs)
						    loaded.insert(id);
					    for (auto ancestor = chain.rbegin(); ancestor != chain.rend(); ++ancestor)
						    if (!loaded.contains(*ancestor))
						    {
							    candidate.load_dependency(
							        environment.library->read_pack_archive(mod_hashes.at(*ancestor)));
							    loaded.insert(*ancestor);
						    }
					    candidate.manifest().erase("dependencies");
					    candidate.manifest()["parent"] = entry.id;
					    document = std::move(candidate);
					    clear_textures();
				    });
		}
		ImGui::EndCombo();
	}
}
} // namespace phoenix
