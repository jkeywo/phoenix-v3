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
void WorkshopWorkspace::State::reset_save_locations()
{
	history.reset(document);
	selection.kind = "Entity";
	saves.reset();
	saving = false;
	pack_location = {};
	library_pack.clear();
	saving_library = false;
	map_locations.clear();
	saving_map.clear();
	rename_from.clear();
	console_rename_from.clear();
}
void WorkshopWorkspace::State::clear_textures()
{
	assets.clear();
	preview.clear();
	map_editor.clear();
}
void WorkshopWorkspace::State::attempt(const std::function<void()> &action)
{
	try
	{
		action();
	}
	catch (const std::exception &error)
	{
		status = error.what();
	}
}
void WorkshopWorkspace::State::close_editor()
{
	map_editor.clear();
	imports.reset();
	saves.reset();
	saving = false;
	clear_textures();
}

void WorkshopWorkspace::State::open_workshop_pack(const std::string &path)
{
	imports.reset();
	document.load_pack(read_pack_path(path).archive());
	reset_save_locations();
	pack_location = {path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1),
	                 path};
	if (std::filesystem::is_directory(std::filesystem::u8path(path)))
		pack_location = {};
	started = true;
	clear_textures();
	pack_name = document.manifest()["id"].get<std::string>();
	selection.definition = document.definitions()["entities"].empty() ? -1 : 0;
	console = document.definitions()["consoles"].empty() ? -1 : 0;
	selection.instance =
	    document.map()["entities"].empty() ? 0 : document.map()["entities"][0]["id"].get<std::uint64_t>();
	status = ToolMessage("Loaded and verified {path}", {{"path", path}});
}

void WorkshopWorkspace::State::render_editor(WorkshopEnvironment &environment)
{
	auto base_archive = [&]
	{
		return installed_base_pack(environment).second;
	};
	if (!started)
	{
		started = true;
		attempt(
		    [&]
		    {
			    open_base_pack(environment);
		    });
	}
	if (started)
	{
		if (const auto catalog = document.preview_ui())
			apply_tool_theme(*catalog);
	}
	else if (environment.session)
		apply_tool_theme(environment.session->catalog());
	auto dock = ImGui::GetID("PhoenixDock");
	auto *viewport = ImGui::GetMainViewport();
	if (dock_size.x != viewport->WorkSize.x || dock_size.y != viewport->WorkSize.y)
	{
		dock_size = viewport->WorkSize;
		ImGui::DockBuilderRemoveNode(dock);
		ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);
		ImGuiID centre = dock, left, right, bottom;
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.10f, &bottom, &centre);
		ImGui::DockBuilderDockWindow(tool_label("Status").c_str(), bottom);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.20f, &left, &centre);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.38f, &right, &centre);
		ImGui::DockBuilderDockWindow(tool_label("Entities").c_str(), left);
		ImGui::DockBuilderDockWindow(tool_label("Definitions").c_str(), left);
		ImGui::DockBuilderDockWindow(tool_label("Map").c_str(), centre);
		for (const char *area : {"State trees", "Scenario", "Test"})
			ImGui::DockBuilderDockWindow(tool_label(area).c_str(), centre);
		ImGui::DockBuilderDockWindow(tool_label("Properties").c_str(), right);
		ImGui::DockBuilderDockWindow(tool_label("Layout canvas").c_str(), centre);
		ImGui::DockBuilderDockWindow(tool_label("Layout properties").c_str(), right);
		ImGui::DockBuilderDockWindow(tool_label("Layout hierarchy").c_str(), left);
		ImGui::DockBuilderDockWindow(tool_label("Layout palette").c_str(), left);
		ImGui::DockBuilderDockWindow(tool_label("Consoles").c_str(), left);
		ImGui::DockBuilderDockWindow(tool_label("Lighting and sprite preview").c_str(), centre);
		ImGui::DockBuilderFinish(dock);
	}
	ImGui::DockSpaceOverViewport(dock, viewport);
	if (auto packaged = packages.take())
	{
		status = packaged->status == ImportStatus::Success
		             ? ToolMessage("Packaged {file}", {{"file", packaged->filename}})
		         : packaged->status == ImportStatus::Cancelled ? "Packaging cancelled"
		                                                       : packaged->error;
#ifndef __EMSCRIPTEN__
		if (packaged->status == ImportStatus::Success)
			try
			{
				const auto name = std::filesystem::path(packaged->destination).filename().string();
				std::map<std::string, Bytes> renamed;
				// The picker may change the archive's basename; regenerate relative companion addresses.
				for (const auto &[old, bytes] : package_files)
				{
					if (old.ends_with(".menu.json"))
					{
						auto menu = parse_json(bytes, "menu");
						for (auto &entry : menu["clients"])
						{
							const std::string path = entry.at("file");
							entry["file"] = name + path.substr(path.find(".clients/"));
						}
						renamed[name + ".menu.json"] = json_bytes(menu);
					}
					else if (const auto at = old.find(".clients/"); at != std::string::npos)
						renamed[name + old.substr(at)] = bytes;
				}
				write_companions(packaged->destination, renamed);
			}
			catch (const std::exception &error)
			{
				status = ToolMessage("Archive saved; companion export failed: {error}", {{"error", error.what()}});
			}
#endif
		package_files.clear();
	}
	if (auto saved = saves.take())
	{
		saving = false;
		if (saved->status == ImportStatus::Success)
		{
			if (saving_library)
				status = ToolMessage("Saved {file}", {{"file", saved->filename}});
			else
			{
				auto &location =
				    saved->purpose == FilePurpose::WorkshopMapSave ? map_locations[saving_map] : pack_location;
				location = {saved->filename, saved->destination};
				if (saved->purpose == FilePurpose::WorkshopPackSave)
					library_pack.clear();
				status = saved->destination.empty()
				             ? "Download requested. This browser cannot overwrite the same file; use a browser "
				               "with file access for Save in place."
				             : ToolMessage("Saved {file}", {{"file", saved->filename}});
			}
		}
		else if (saved->status == ImportStatus::Cancelled)
			status = "Save cancelled; the previous save location is unchanged.";
		else
			status = saved->error;
		saving_library = false;
	}
	if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
		attempt(
		    [&]
		    {
			    save_pack(environment, true);
		    });
	else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
		attempt(
		    [&]
		    {
			    save_pack(environment, false);
		    });
	attempt(
	    [&]
	    {
		    if (auto imported = imports.take(); imported && imported->status != ImportStatus::Cancelled)
		    {
			    if (!imported->error.empty())
				    throw std::runtime_error(imported->error);
			    {
				    if (imported->purpose == FilePurpose::WorkshopPack)
				    {
					    document.load_pack(imported->bytes);
					    reset_save_locations();
					    pack_location = {imported->filename, imported->destination};
					    clear_textures();
					    selection.instance = 0;
					    selection.definition = -1;
					    console = document.definitions()["consoles"].empty() ? -1 : 0;
					    pack_name = document.manifest()["id"];
				    }
				    if (imported->purpose == FilePurpose::WorkshopLibrary)
				    {
					    document.load_dependency(imported->bytes);
					    clear_textures();
				    }
				    if (imported->purpose == FilePurpose::WorkshopMap)
				    {
					    document.load_map(imported->bytes);
					    map_locations[document.map().at("id").get<std::string>()] = {imported->filename,
					                                                                 imported->destination};
					    selection.instance = 0;
				    }
				    if (imported->purpose == FilePurpose::WorkshopModel)
				    {
					    const auto ref = document.import_model(imported->filename, std::move(imported->bytes));
					    auto &entities = document.definitions()["entities"];
					    if (selection.definition >= 0 && selection.definition < static_cast<int>(entities.size()))
						    entities[selection.definition]["render"]["model"]["asset"] = ref;
					    clear_textures();
				    }
				    if (imported->purpose == FilePurpose::WorkshopFont)
				    {
					    document.import_font(imported->filename, std::move(imported->bytes));
				    }
				    if (imported->purpose == FilePurpose::WorkshopImage)
				    {
					    document.import_image(imported->filename, std::move(imported->bytes));
					    clear_textures();
				    }
				    if (imported->purpose == FilePurpose::WorkshopSprite)
				    {
					    const auto sidecar = parse_json(imported->bytes, "sprite sidecar");
					    if (!sidecar.is_object() || !sidecar.contains("files") || !sidecar.at("files").is_object())
						    throw std::runtime_error("Sprite sidecar requires a files object");
					    pending_sidecar = sidecar;
					    import_engines = sidecar.contains("engine_effect");
					    sidecar_images.clear();
					    for (const char *key : {"sprite", "normal", "depth", "radar_icon"})
					    {
						    const auto file = pending_sidecar.at("files").value(key, Json());
						    if (!file.is_string())
							    continue;
						    const auto filename = file.get<std::string>();
						    const auto leaf = filename.substr(filename.find_last_of("/\\") == std::string::npos
						                                          ? 0
						                                          : filename.find_last_of("/\\") + 1);
						    const auto path = "assets/" + leaf;
						    if (document.pack().files.contains(path))
							    sidecar_images[key] = document.manifest().at("id").get<std::string>() + ":" + path;
						    else
						    {
							    std::vector<std::string> matches;
							    for (const auto &[asset, bytes] : document.pack().files)
								    if (asset.starts_with("assets/") && asset.ends_with("/" + leaf))
									    matches.push_back(document.manifest().at("id").get<std::string>() + ":" +
									                      asset);
							    for (const auto &[reference, bytes] : document.libraries().assets)
								    if (reference.ends_with("/" + leaf))
									    matches.push_back(reference);
							    if (matches.size() == 1)
								    sidecar_images[key] = matches.front();
						    }
					    }
				    }
				    status = ToolMessage("Imported {file}", {{"file", imported->filename}});
			    }
		    }
	    });

	auto restore_history = [&](bool redo)
	{
		if (redo ? history.redo(document) : history.undo(document))
		{
			clear_textures();
			pack_name = document.manifest().at("id").get<std::string>();
			rename_from.clear();
			console_rename_from.clear();
			status = redo ? "Workshop edit redone." : "Workshop edit undone.";
		}
	};
	if (!ImGui::GetIO().WantTextInput)
	{
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
			restore_history(false);
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
		    ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
			restore_history(true);
	}
	if (ImGui::BeginMainMenuBar())
	{
		if (!environment.embedded && ImGui::BeginMenu(tool_label("Phoenix").c_str()))
		{
			if (ImGui::MenuItem(tool_label(environment.active ? "Settings" : "Home").c_str()))
				environment.navigate(environment.active ? "settings" : "home");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(tool_label("File").c_str(), !saving))
		{
			if (ImGui::MenuItem(tool_label("New pack").c_str()))
			{
				imports.reset();
				document.reset();
				reset_save_locations();
				attempt(
				    [&]
				    {
					    document.load_dependency(base_archive());
				    });
				pending_sidecar = nullptr;
				sidecar_images.clear();
				clear_textures();
				selection.instance = 0;
				selection.definition = -1;
				console = -1;
				pack_name = "workshop";
				history.reset(document);
			}
			if (ImGui::MenuItem(tool_label("Open pack...").c_str()))
				attempt(
				    [&]
				    {
					    request_file(imports, FilePurpose::WorkshopPack);
				    });
			if (ImGui::MenuItem(tool_label("Open base game pack").c_str()))
				attempt(
				    [&]
				    {
					    open_base_pack(environment);
				    });
			if (ImGui::MenuItem(tool_label("Load dependency pack...").c_str()))
				attempt(
				    [&]
				    {
					    request_file(imports, FilePurpose::WorkshopLibrary);
				    });
			if (ImGui::MenuItem(tool_label("Save").c_str(), "Ctrl+S", false, !saving))
				attempt(
				    [&]
				    {
					    save_pack(environment, false);
				    });
			if (ImGui::MenuItem(tool_label("Save as...").c_str(), "Ctrl+Shift+S", false, !saving))
				attempt(
				    [&]
				    {
					    save_pack(environment, true);
				    });
			if (ImGui::BeginMenu(tool_label("Package...").c_str()))
			{
				for (const bool scenario : {false, true})
					if (ImGui::BeginMenu(tool_label(scenario ? "Selected scenario" : "Game pack").c_str()))
					{
						for (const auto encoding : {PackEncoding::Zip, PackEncoding::Dat})
							if (ImGui::MenuItem(
							        tool_label(encoding == PackEncoding::Zip ? "Source ZIP..." : "Compiled DAT...")
							            .c_str()))
								attempt(
								    [&]
								    {
									    const auto name =
									        document.manifest().at("id").get<std::string>() +
									        (scenario ? "-" + document.map().at("id").get<std::string>() : "") +
									        (encoding == PackEncoding::Zip ? ".zip" : ".dat");
									    package_files = document.export_companions(name, encoding, scenario);
#ifdef __EMSCRIPTEN__
									    request_save(packages, FilePurpose::WorkshopPackSave, name + ".bundle.zip",
									                 distribution_zip(package_files), {}, true);
#else
									    request_save(packages, FilePurpose::WorkshopPackSave, name,
									                 package_files.at(name), {}, true);
#endif
									    status = "Packaging...";
								    });
						ImGui::EndMenu();
					}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem(tool_label("New map").c_str()))
				attempt(
				    [&]
				    {
					    add_map();
				    });
			if (ImGui::MenuItem(tool_label("Load map...").c_str()))
				attempt(
				    [&]
				    {
					    request_file(imports, FilePurpose::WorkshopMap);
				    });
			if (ImGui::MenuItem(tool_label("Save map").c_str(), nullptr, false, !saving))
				attempt(
				    [this]
				    {
					    save_map(false);
				    });
			if (ImGui::MenuItem(tool_label("Save map as...").c_str(), nullptr, false, !saving))
				attempt(
				    [this]
				    {
					    save_map(true);
				    });
			if (ImGui::MenuItem(tool_label("Import PNG...").c_str()))
				attempt(
				    [&]
				    {
					    request_file(imports, FilePurpose::WorkshopImage);
				    });
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(tool_label("Edit").c_str()))
		{
			history.checkpoint(document);
			if (ImGui::MenuItem(tool_label("Undo").c_str(), "Ctrl+Z", false, history.can_undo()))
				restore_history(false);
			if (ImGui::MenuItem(tool_label("Redo").c_str(), "Ctrl+Y", false, history.can_redo()))
				restore_history(true);
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem(tool_label("Verify").c_str(), nullptr, false, true))
			attempt(
			    [&]
			    {
				    document.verify();
				    status = "Definitions, PNGs, map references, slots and widget bindings verified.";
			    });
		ToolPreferences preferences{environment.preferences.scale, environment.preferences.language,
		                            environment.preferences.pseudo};
		if (draw_tool_preferences(preferences))
		{
			environment.preferences.scale = preferences.scale;
			environment.preferences.language = preferences.language;
			environment.preferences.pseudo = preferences.pseudo;
			environment.persist_preferences(environment.preferences);
		}
		ImGui::EndMainMenuBar();
	}

	tool_window(tool_label("Entities").c_str());
	for (const auto &instance : document.map()["entities"])
	{
		auto label = std::to_string(instance.at("id").get<std::uint64_t>()) + " / " +
		             instance.at("definition").get<std::string>();
		if (ImGui::Selectable(label.c_str(), selection.kind == "Entity" && selection.instance == instance["id"]))
		{
			selection.instance = instance["id"];
			selection.kind = "Entity";
		}
	}
	if (selection.kind == "Entity" && selection.instance)
	{
		if (tool_button(tool_label("Duplicate").c_str()))
			document.duplicate(selection.instance);
		if (tool_button(tool_label("Delete").c_str()))
		{
			document.erase(selection.instance);
			selection.instance = 0;
		}
	}
	ImGui::End();
	tool_window(tool_label("Definitions").c_str());

	for (const char *area : {"Templates and assemblies", "Knowledge", "Coordination"})
		if (ImGui::Selectable(tool_label(area).c_str(), selection.kind == area))
			selection.kind = area;
	ImGui::Separator();
	{
		Json background = document.manifest().value("loading_background", Json(""));
		assets.pick(document, "Pack loading background", background);
		if (background.is_string() && !background.get<std::string>().empty())
			document.manifest()["loading_background"] = background;
		else
			document.manifest().erase("loading_background");
	}
	pack_parent_properties(environment);
	ImGui::InputText(tool_label("Pack ID").c_str(), &pack_name);
	if (tool_button(tool_label("Apply pack ID").c_str()))
		attempt(
		    [&]
		    {
			    document.rename_pack(pack_name);
			    clear_textures();
		    });
	if (tool_button(tool_label("Import PNG").c_str()))
		attempt(
		    [&]
		    {
			    request_file(imports, FilePurpose::WorkshopImage);
		    });
	if (tool_button(tool_label("New entity definition").c_str()))
	{
		document.create_entity();
		selection.definition = static_cast<int>(document.definitions()["entities"].size() - 1);
		selection.kind = "Entity";
		selection.instance = 0;
	}
	auto &definitions = document.definitions()["entities"];
	for (std::size_t i = 0; i < definitions.size(); ++i)
	{
		auto id = definitions[i].at("id").get<std::string>();
		const auto name = definitions[i].value("name", std::string{});
		const auto label = (name.empty() ? id : name + " (" + id + ")") + "###" + id;
		if (ImGui::Selectable(label.c_str(), selection.kind == "Entity" && selection.definition == i))
		{
			selection.definition = static_cast<int>(i);
			selection.kind = "Entity";
			selection.instance = 0;
		}
	}
	if (!document.libraries().entities.empty() && ImGui::CollapsingHeader(tool_label("Dependency entities").c_str()))
	{
		for (const auto &[id, entity] : document.libraries().entities)
			if (ImGui::Selectable(id.c_str(), library_parent == id))
				library_parent = id;
		if (!library_parent.empty() && tool_button(tool_label("Create derived entity").c_str()))
			attempt(
			    [&]
			    {
				    document.create_entity(library_parent);
				    selection.definition = static_cast<int>(definitions.size() - 1);
				    selection.kind = "Entity";
				    selection.instance = 0;
			    });
	}
	if (ImGui::CollapsingHeader(tool_label("Override dependency definitions").c_str()))
	{
		ImGui::TextWrapped(
		    "%s", localise("Copy a dependency definition into this pack and explicitly declare its override.").c_str());
		auto overrides = [&](const char *kind, const auto &table)
		{
			for (const auto &[id, value] : table)
				if (tool_button((std::string("Override ") + kind + " / " + id).c_str()))
				{
					auto &local = document.definitions()[kind];
					const bool exists = std::any_of(local.begin(), local.end(),
					                                [&](const auto &entry)
					                                {
						                                return entry.at("id") == id;
					                                });
					if (exists)
					{
						status = ToolMessage("This definition is already editable in the pack: {id}", {{"id", id}});
						continue;
					}
					local.push_back(value);
					auto &declared = document.manifest()["overrides"];
					if (!declared.is_array())
						declared = Json::array();
					if (std::find(declared.begin(), declared.end(), Json(id)) == declared.end())
						declared.push_back(id);
					status = ToolMessage("Explicit override added: {id}", {{"id", id}});
				}
		};
		overrides("entities", document.libraries().entities);
		overrides("maps", document.libraries().maps);
		overrides("consoles", document.libraries().consoles);
		overrides("trees", document.libraries().trees);
		overrides("layouts", document.libraries().layouts);
		overrides("ui_groups", document.libraries().ui_groups);
		overrides("themes", document.libraries().themes);
	}
	ImGui::Separator();
	ImGui::TextWrapped("%s", status.c_str());
	ImGui::End();

	tool_window(tool_label("Consoles").c_str());
	console_editor(environment, false);
	ImGui::End();
	const auto &map_frame = environment.frame;
	map_editor.draw(document, selection, status, saving, map_frame,
	                [&]
	                {
		                save_pack(environment, false);
	                });
	gameplay.areas(document, environment.preferences.pseudo, status, selection.kind);
	tool_window(tool_label("Properties").c_str());
	if (selection.kind == "Entity")
	{
		if (auto *instance = selected_instance())
		{
			ImGui::TextUnformatted(ToolMessage("Instance {id}", {{"id", std::to_string(selection.instance)}}).c_str());
			transform_fields(*instance);
			string_field("Faction", *instance, "faction");
			ImGui::Separator();
			selection.definition = -1;
			for (std::size_t i = 0; i < definitions.size(); ++i)
				if (definitions[i]["id"] == (*instance)["definition"])
					selection.definition = static_cast<int>(i);
		}
		if (selection.definition >= 0 && selection.definition < static_cast<int>(definitions.size()))
		{
			auto &definition = definitions[selection.definition];
			ImGui::TextUnformatted(definition.at("id").get_ref<const std::string &>().c_str());
			if (ImGui::CollapsingHeader(tool_label("Appearance and physics").c_str()))
				entity_properties(definition);
			gameplay.properties(document, environment.preferences.pseudo, status, selection.kind,
			                    definition.at("id").get<std::string>());
		}
		else
			ImGui::TextWrapped("%s", localise("Create or select a reusable entity definition.").c_str());
	}
	else if (selection.kind == "Console")
		console_editor(environment, true);
	else if (selection.kind == "Ship slots")
		slots_editor();
	else
		gameplay.properties(document, environment.preferences.pseudo, status, selection.kind, "");
	ImGui::End();

	if (selection.kind == "Console")
		layout_editor.draw(document, console, environment.preferences.pseudo, imports, status);
	preview.draw(document, selection.definition, status);
	if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		history.checkpoint(document);

	tool_window(tool_label("Status").c_str());
	if (!environment.status.empty())
		ImGui::TextWrapped("%s", localise(environment.status).c_str());
	ImGui::TextWrapped("%s", status.c_str());
	ImGui::End();
}
WorkshopWorkspace::WorkshopWorkspace() : state_(std::make_unique<State>())
{
}
WorkshopWorkspace::~WorkshopWorkspace() = default;
void WorkshopWorkspace::draw(WorkshopEnvironment &environment)
{
	state_->render_editor(environment);
}
void WorkshopWorkspace::close()
{
	state_->close_editor();
}
void WorkshopWorkspace::open_pack(const std::string &path)
{
	state_->open_workshop_pack(path);
}
bool WorkshopWorkspace::probe_layout(unsigned stage)
{
	return state_->probe_workshop_layout(stage);
}
bool WorkshopWorkspace::probe_controllers(unsigned stage)
{
	return state_->probe_workshop_controllers(stage);
}
bool WorkshopWorkspace::probe_test(WorkshopDocument &document, unsigned stage)
{
	return state_->gameplay.probe_test(document, stage);
}
void WorkshopWorkspace::set_vocabulary(const Json &vocabulary)
{
	state_->gameplay.set_vocabulary(vocabulary);
}
void WorkshopWorkspace::set_feedback(const Json &feedback)
{
	state_->gameplay.set_feedback(feedback);
}
std::optional<GameplayTestRequest> WorkshopWorkspace::take_request()
{
	return state_->gameplay.take_request();
}

} // namespace phoenix
