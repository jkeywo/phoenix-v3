module;
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.tools;
import phoenix.content;
import phoenix.files;
import phoenix.presentation;
import phoenix.session.api;

namespace phoenix
{
namespace
{
struct Editor
{
	WorkshopDocument document;
	std::uint64_t selected = 0;
	int definition = -1, console = -1;
	std::string status = "Import a PNG, create an entity definition, then place instances on the map.";
	std::string pack_name = "workshop", library_parent;
	std::map<std::string, Texture2D> textures;
	float pan_x = 0, pan_y = 0, zoom = 0.004f;
	void clear_textures()
	{
		for (auto [id, texture] : textures)
			UnloadTexture(texture);
		textures.clear();
	}
	Texture2D texture(const std::string &id)
	{
		if (textures.contains(id))
			return textures.at(id);
		auto prefix = document.manifest().at("id").get<std::string>() + ":";
		const Bytes *bytes = nullptr;
		if (id.starts_with(prefix))
		{
			auto found = document.pack().files.find(id.substr(prefix.size()));
			if (found != document.pack().files.end())
				bytes = &found->second;
		}
		else
		{
			auto found = document.libraries().assets.find(id);
			if (found != document.libraries().assets.end())
				bytes = &found->second;
		}
		if (!bytes)
			return {};
		auto image = LoadImageFromMemory(".png", bytes->data(), static_cast<int>(bytes->size()));
		if (!image.data)
			return {};
		auto result = LoadTextureFromImage(image);
		UnloadImage(image);
		textures[id] = result;
		return result;
	}
} editor;
template <class Function> void attempt(Function action)
{
	try
	{
		action();
	}
	catch (const std::exception &error)
	{
		editor.status = error.what();
	}
}
void string_field(const char *label, Json &object, const char *key)
{
	std::string value = object.value(key, std::string{});
	ImGui::TextWrapped("%s", label);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputText((std::string("##") + label).c_str(), &value))
		object[key] = value;
}
void integer_field(const char *label, Json &object, const char *key)
{
	std::int64_t value = object.value(key, static_cast<std::int64_t>(0));
	ImGui::TextWrapped("%s", label);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputScalar((std::string("##") + label).c_str(), ImGuiDataType_S64, &value))
		object[key] = value;
}
void transform_fields(Json &value)
{
	integer_field("X (mm)", value, "x_mm");
	integer_field("Y (mm)", value, "y_mm");
	integer_field("Heading (millidegrees)", value, "heading_mdeg");
}
Json *selected_instance()
{
	for (auto &instance : editor.document.map()["entities"])
		if (instance["id"] == editor.selected)
			return &instance;
	return nullptr;
}
void asset_picker(const char *label, Json &value)
{
	std::string current = value.is_string() ? value.get<std::string>() : "";
	ImGui::TextWrapped("%s", label);
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo((std::string("##") + label).c_str(), current.empty() ? "Choose PNG" : current.c_str()))
	{
		for (const auto &[path, bytes] : editor.document.pack().files)
			if (path.starts_with("assets/"))
			{
				auto reference = editor.document.manifest().at("id").get<std::string>() + ":" + path;
				if (ImGui::Selectable(reference.c_str(), reference == current))
					value = reference;
			}
		for (const auto &[reference, bytes] : editor.document.libraries().assets)
			if (ImGui::Selectable(reference.c_str(), reference == current))
				value = reference;
		ImGui::EndCombo();
	}
}
void entity_properties(Json &entity)
{
	ImGui::TextWrapped("Reusable definition: %s", entity.at("id").get_ref<const std::string &>().c_str());
	if (entity.contains("extends"))
	{
		ImGui::TextWrapped("Inherited definition. Flatten before editing its resolved fields.");
		if (ImGui::Button("Flatten definition"))
			attempt(
			    [&]
			    {
				    entity = editor.document.verify().entities.at(entity.at("id").get<std::string>());
			    });
		return;
	}
	auto &render = entity["render"];
	bool eight = render["mode"] == "eight";
	if (ImGui::Checkbox("Eight-direction billboard", &eight))
	{
		render["mode"] = eight ? "eight" : "camera";
		std::string image = render["images"].empty() ? "" : render["images"][0].get<std::string>();
		render["images"] = Json::array();
		for (int i = 0; i < (eight ? 8 : 1); ++i)
			render["images"].push_back(image);
	}
	double scale = render.value("scale", 1.0);
	if (ImGui::InputDouble("Render scale", &scale))
		render["scale"] = scale;
	for (std::size_t i = 0; i < render["images"].size(); ++i)
	{
		ImGui::PushID(static_cast<int>(i));
		asset_picker(eight ? "Bearing image" : "Camera-facing image", render["images"][i]);
		ImGui::PopID();
	}
	auto &systems = entity["systems"];
	if (ImGui::CollapsingHeader("Systems", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const char *name : {"engine", "steering", "radar"})
		{
			bool enabled = systems.contains(name);
			if (ImGui::Checkbox(name, &enabled))
			{
				if (!enabled)
					systems.erase(name);
				else if (std::string(name) == "engine")
					systems[name] = {{"max_speed_mm_s", 30000}, {"acceleration_mm_s2", 3000}};
				else if (std::string(name) == "steering")
					systems[name] = {{"turn_mdeg_s", 45000}};
				else
					systems[name] = {{"range_mm", 200000}};
			}
			if (!enabled)
				continue;
			ImGui::PushID(name);
			if (std::string(name) == "engine")
			{
				integer_field("Max speed (mm/s)", systems[name], "max_speed_mm_s");
				integer_field("Acceleration (mm/s2)", systems[name], "acceleration_mm_s2");
			}
			if (std::string(name) == "steering")
				integer_field("Turn rate (millidegrees/s)", systems[name], "turn_mdeg_s");
			if (std::string(name) == "radar")
				integer_field("Range (mm)", systems[name], "range_mm");
			ImGui::PopID();
		}
	}
	if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto &collision = entity["collision"];
		bool box = collision["shape"] == "box";
		if (ImGui::Checkbox("Oriented box", &box))
		{
			collision["shape"] = box ? "box" : "circle";
			if (box)
			{
				collision["half_width_mm"] = 2000;
				collision["half_length_mm"] = 4000;
				collision.erase("radius_mm");
			}
			else
			{
				collision["radius_mm"] = 2000;
				collision.erase("half_width_mm");
				collision.erase("half_length_mm");
			}
		}
		if (box)
		{
			integer_field("Half width (mm)", collision, "half_width_mm");
			integer_field("Half length (mm)", collision, "half_length_mm");
		}
		else
			integer_field("Radius (mm)", collision, "radius_mm");
		integer_field("Mass (grams; 0 = static)", collision, "mass_grams");
	}
	std::string console = entity.value("console", std::string{});
	std::string controller = entity.value("controller", std::string{});
	if (ImGui::BeginCombo("Delegated state tree", controller.empty() ? "None" : controller.c_str()))
	{
		if (ImGui::Selectable("None", controller.empty()))
			entity.erase("controller");
		for (const auto &value : editor.document.definitions()["trees"])
		{
			auto id = value.at("id").get<std::string>();
			if (ImGui::Selectable(id.c_str(), id == controller))
				entity["controller"] = id;
		}
		for (const auto &[id, value] : editor.document.libraries().trees)
			if (ImGui::Selectable(id.c_str(), id == controller))
				entity["controller"] = id;
		ImGui::EndCombo();
	}
	if (ImGui::BeginCombo("Console", console.empty() ? "None" : console.c_str()))
	{
		if (ImGui::Selectable("None", console.empty()))
			entity.erase("console");
		for (const auto &value : editor.document.definitions()["consoles"])
		{
			auto id = value.at("id").get<std::string>();
			if (ImGui::Selectable(id.c_str(), id == console))
				entity["console"] = id;
		}
		ImGui::EndCombo();
	}
}
void console_editor()
{
	auto &consoles = editor.document.definitions()["consoles"];
	if (ImGui::Button("New console"))
	{
		std::string id =
		    editor.document.manifest().at("id").get<std::string>() + ".console" + std::to_string(consoles.size() + 1);
		consoles.push_back({{"id", id}, {"widgets", Json::array()}});
		editor.console = static_cast<int>(consoles.size() - 1);
	}
	for (std::size_t i = 0; i < consoles.size(); ++i)
	{
		auto id = consoles[i].at("id").get<std::string>();
		if (ImGui::Selectable(id.c_str(), editor.console == i))
			editor.console = static_cast<int>(i);
	}
	if (editor.console < 0 || editor.console >= static_cast<int>(consoles.size()))
		return;
	auto &widgets = consoles[editor.console]["widgets"];
	const std::pair<const char *, const char *> palette[] = {{"radar", "radar.contacts"},
	                                                         {"throttle", "engine.desired_speed"},
	                                                         {"heading", "steering.desired_heading"},
	                                                         {"status", "ship.status"}};
	for (auto [kind, binding] : palette)
	{
		if (ImGui::Button((std::string("Add ") + kind).c_str()))
		{
			std::string id;
			unsigned count = 1;
			do
			{
				id = std::string(kind) + std::to_string(count++);
			} while (std::any_of(widgets.begin(), widgets.end(),
			                     [&](const auto &w)
			                     {
				                     return w["id"] == id;
			                     }));
			widgets.push_back({{"id", id},
			                   {"kind", kind},
			                   {"binding", binding},
			                   {"label", kind},
			                   {"x", 0},
			                   {"y", 0},
			                   {"width", 500},
			                   {"height", 500}});
		}
	}
	int remove = -1;
	for (std::size_t i = 0; i < widgets.size(); ++i)
	{
		auto &widget = widgets[i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(widget.at("id").get_ref<const std::string &>().c_str()))
		{
			string_field("Semantic label", widget, "label");
			ImGui::TextWrapped("Binding: %s", widget["binding"].get_ref<const std::string &>().c_str());
			for (const char *key : {"x", "y", "width", "height"})
			{
				int value = widget[key];
				if (ImGui::SliderInt(key, &value, (std::string(key) == "x" || std::string(key) == "y") ? 0 : 1, 1000))
					widget[key] = value;
			}
			if (ImGui::Button("Delete widget"))
				remove = static_cast<int>(i);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (remove >= 0)
		widgets.erase(widgets.begin() + remove);
	ImGui::SeparatorText("Layout preview (no live bindings yet)");
	auto origin = ImGui::GetCursorScreenPos();
	float scale = std::max(0.1f, ImGui::GetContentRegionAvail().x / 1000);
	ImGui::Dummy({1000 * scale, 1000 * scale});
	auto *draw = ImGui::GetWindowDrawList();
	for (const auto &widget : widgets)
	{
		ImVec2 p{origin.x + widget["x"].get<float>() * scale, origin.y + widget["y"].get<float>() * scale};
		ImVec2 end{p.x + widget["width"].get<float>() * scale, p.y + widget["height"].get<float>() * scale};
		draw->AddRect(p, end, IM_COL32(118, 201, 232, 255));
		draw->AddText(p, IM_COL32_WHITE, widget["label"].get_ref<const std::string &>().c_str());
	}
}
void slots_editor()
{
	auto &slots = editor.document.map()["ship_slots"];
	if (ImGui::Button("Add ship slot"))
	{
		std::string id;
		unsigned count = 1;
		do
		{
			id = "slot" + std::to_string(count++);
		} while (std::any_of(slots.begin(), slots.end(),
		                     [&](const auto &slot)
		                     {
			                     return slot["id"] == id;
		                     }));
		slots.push_back({{"id", id}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}, {"allowed_ships", Json::array()}});
	}
	int remove = -1;
	for (std::size_t i = 0; i < slots.size(); ++i)
	{
		auto &slot = slots[i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::TreeNode(slot["id"].get_ref<const std::string &>().c_str()))
		{
			string_field("Name", slot, "id");
			transform_fields(slot);
			ImGui::TextUnformatted("Allowed ships:");
			for (const auto &entity : editor.document.definitions()["entities"])
			{
				std::string id = entity["id"];
				auto &allowed = slot["allowed_ships"];
				auto found = std::find(allowed.begin(), allowed.end(), Json(id));
				bool selected = found != allowed.end();
				if (ImGui::Checkbox(id.c_str(), &selected))
				{
					if (selected)
						allowed.push_back(id);
					else
						allowed.erase(found);
				}
			}
			for (const auto &[id, entity] : editor.document.libraries().entities)
			{
				auto &allowed = slot["allowed_ships"];
				auto found = std::find(allowed.begin(), allowed.end(), Json(id));
				bool selected = found != allowed.end();
				if (ImGui::Checkbox(id.c_str(), &selected))
				{
					if (selected)
						allowed.push_back(id);
					else
						allowed.erase(found);
				}
			}
			if (ImGui::Button("Remove slot"))
				remove = static_cast<int>(i);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (remove >= 0)
		slots.erase(slots.begin() + remove);
}
void map_view()
{
	ImGui::Begin("Map");
	if (ImGui::BeginCombo("Map", editor.document.map().at("id").get_ref<const std::string &>().c_str()))
	{
		auto &maps = editor.document.definitions()["maps"];
		for (std::size_t i = 0; i < maps.size(); ++i)
			if (ImGui::Selectable(maps[i].at("id").get_ref<const std::string &>().c_str()))
			{
				editor.document.select_map(i);
				editor.selected = 0;
			}
		ImGui::EndCombo();
	}
	ImGui::TextWrapped("Double-click: place selected definition | drag: move | middle-drag: pan | wheel: zoom");
	auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
	size.x = std::max(size.x, 1.0f);
	size.y = std::max(size.y, 1.0f);
	ImGui::InvisibleButton("map-canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	bool hovered = ImGui::IsItemHovered();
	auto &io = ImGui::GetIO();
	if (hovered)
		editor.zoom = std::clamp(editor.zoom * std::pow(1.15f, io.MouseWheel), 0.00005f, 0.1f);
	if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
	{
		editor.pan_x += io.MouseDelta.x;
		editor.pan_y += io.MouseDelta.y;
	}
	ImVec2 centre{origin.x + size.x / 2 + editor.pan_x, origin.y + size.y / 2 + editor.pan_y};
	auto *draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
	draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(8, 15, 25, 255));
	float grid = 10000 * editor.zoom;
	while (grid < 25)
		grid *= 10;
	while (grid > 250)
		grid /= 10;
	float first_x = origin.x + std::fmod(centre.x - origin.x, grid),
	      first_y = origin.y + std::fmod(centre.y - origin.y, grid);
	for (float x = first_x; x < origin.x + size.x; x += grid)
		draw->AddLine({x, origin.y}, {x, origin.y + size.y}, IM_COL32(32, 58, 70, 255));
	for (float y = first_y; y < origin.y + size.y; y += grid)
		draw->AddLine({origin.x, y}, {origin.x + size.x, y}, IM_COL32(32, 58, 70, 255));
	std::uint64_t nearest = 0;
	float distance = 24;
	auto &definitions = editor.document.definitions()["entities"];
	for (const auto &instance : editor.document.map()["entities"])
	{
		ImVec2 p{centre.x + instance["x_mm"].get<float>() * editor.zoom,
		         centre.y - instance["y_mm"].get<float>() * editor.zoom};
		auto definition = std::find_if(definitions.begin(), definitions.end(),
		                               [&](const auto &entity)
		                               {
			                               return entity["id"] == instance["definition"];
		                               });
		if (definition != definitions.end() && definition->contains("render"))
		{
			const auto &render = (*definition)["render"];
			if (render.contains("images") && !render["images"].empty() && render["images"][0].is_string())
			{
				auto texture = editor.texture(render["images"][0]);
				if (texture.id)
				{
					float half = std::clamp(render.value("scale", 1.0f) * 16, 2.0f, 200.0f);
					draw->AddImage(static_cast<ImTextureID>(texture.id), {p.x - half, p.y - half},
					               {p.x + half, p.y + half});
				}
			}
		}
		bool selected = instance["id"] == editor.selected;
		draw->AddCircle(p, selected ? 14.0f : 8.0f,
		                selected ? IM_COL32(255, 205, 90, 255) : IM_COL32(118, 201, 232, 255));
		float angle = instance["heading_mdeg"].get<float>() * 3.14159265f / 180000;
		draw->AddLine(p, {p.x + 20 * std::cos(angle), p.y - 20 * std::sin(angle)}, IM_COL32(118, 201, 232, 255));
		float d = std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y);
		if (d < distance)
		{
			distance = d;
			nearest = instance["id"];
		}
	}
	draw->PopClipRect();
	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && editor.definition >= 0 &&
	    editor.definition < static_cast<int>(definitions.size()))
	{
		editor.selected = editor.document.place(definitions[editor.definition]["id"],
		                                        static_cast<std::int64_t>((io.MousePos.x - centre.x) / editor.zoom),
		                                        static_cast<std::int64_t>((centre.y - io.MousePos.y) / editor.zoom));
	}
	else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		editor.selected = nearest;
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		if (auto *instance = selected_instance())
		{
			(*instance)["x_mm"] = static_cast<std::int64_t>((io.MousePos.x - centre.x) / editor.zoom);
			(*instance)["y_mm"] = static_cast<std::int64_t>((centre.y - io.MousePos.y) / editor.zoom);
		}
	}
	ImGui::End();
}
} // namespace
void close_editor()
{
	editor.clear_textures();
}
void open_workshop_pack(const std::string &path)
{
	editor.document.load_pack(read_file(path));
	editor.clear_textures();
	editor.pack_name = editor.document.manifest()["id"].get<std::string>();
	editor.definition = editor.document.definitions()["entities"].empty() ? -1 : 0;
	editor.selected =
	    editor.document.map()["entities"].empty() ? 0 : editor.document.map()["entities"][0]["id"].get<std::uint64_t>();
	editor.status = "Loaded and verified " + path;
}
void render_editor(Application &app)
{
	static bool docked = false;
	auto dock = ImGui::GetID("PhoenixDock");
	auto *viewport = ImGui::GetMainViewport();
	if (!docked)
	{
		docked = true;
		ImGui::DockBuilderRemoveNode(dock);
		ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);
		ImGuiID centre = dock, left, right, bottom;
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.10f, &bottom, &centre);
		ImGui::DockBuilderDockWindow("Status", bottom);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.20f, &left, &centre);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.38f, &right, &centre);
		ImGui::DockBuilderDockWindow("Entities", left);
		ImGui::DockBuilderDockWindow("Definitions", left);
		ImGui::DockBuilderDockWindow("Properties", right);
		ImGui::DockBuilderDockWindow("Consoles", right);
		ImGui::DockBuilderDockWindow("Ship slots", right);
		ImGui::DockBuilderDockWindow("Map", centre);
		ImGui::DockBuilderFinish(dock);
	}
	ImGui::DockSpaceOverViewport(dock, viewport);
	bool readonly = app.screen == Screen::gm;
	attempt(
	    [&]
	    {
		    if (auto imported = take_imported_file())
		    {
			    if (!imported->error.empty())
				    throw std::runtime_error(imported->error);
			    if (!readonly)
			    {
				    if (imported->purpose == "pack")
				    {
					    editor.document.load_pack(imported->bytes);
					    editor.clear_textures();
					    editor.selected = 0;
					    editor.definition = -1;
					    editor.console = -1;
					    editor.pack_name = editor.document.manifest()["id"];
				    }
				    if (imported->purpose == "library")
				    {
					    editor.document.load_dependency(imported->bytes);
					    editor.clear_textures();
				    }
				    if (imported->purpose == "map")
				    {
					    editor.document.load_map(imported->bytes);
					    editor.selected = 0;
				    }
				    if (imported->purpose == "image")
					    editor.document.import_image(imported->filename, std::move(imported->bytes));
				    editor.status = "Imported " + imported->filename;
			    }
		    }
	    });
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("Phoenix"))
		{
			if (ImGui::MenuItem("Home"))
				app.screen = Screen::home;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("File", !readonly))
		{
			if (ImGui::MenuItem("New pack"))
			{
				editor.document.reset();
				editor.clear_textures();
				editor.selected = 0;
				editor.definition = -1;
				editor.console = -1;
				editor.pack_name = "workshop";
			}
			if (ImGui::MenuItem("Open pack..."))
				request_file("pack");
			if (ImGui::MenuItem("Load dependency pack..."))
				request_file("library");
			if (ImGui::MenuItem("Export verified pack..."))
				attempt(
				    [&]
				    {
					    export_file(editor.document.manifest()["id"].get<std::string>() + ".zip",
					                editor.document.export_pack());
					    editor.status = "Pack verified and exported.";
				    });
			ImGui::Separator();
			if (ImGui::MenuItem("New map"))
				attempt(
				    [&]
				    {
					    editor.document.new_map("workshop.map" +
					                            std::to_string(editor.document.definitions()["maps"].size() + 1));
				    });
			if (ImGui::MenuItem("Load map..."))
				request_file("map");
			if (ImGui::MenuItem("Save map..."))
				attempt(
				    [&]
				    {
					    export_file(editor.document.map()["id"].get<std::string>() + ".json",
					                editor.document.save_map());
				    });
			if (ImGui::MenuItem("Import PNG..."))
				request_file("image");
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Verify", nullptr, false, !readonly))
			attempt(
			    [&]
			    {
				    editor.document.verify();
				    editor.status = "Definitions, PNGs, map references, slots and widget bindings verified.";
			    });
		ImGui::EndMainMenuBar();
	}
	if (readonly)
	{
		const auto state = app.session ? app.session->inspection() : Json(nullptr);
		ImGui::Begin("Status");
		ImGui::TextWrapped("%s", app.session ? app.session->connection_status().c_str()
		                                     : "Connect to a ship server to request read-only inspection.");
		ImGui::End();
		if (state.is_null())
		{
			ImGui::Begin("Map");
			ImGui::TextWrapped("Waiting for host approval. Live world data is unavailable.");
			ImGui::End();
			return;
		}
		ImGui::Begin("Entities");
		for (const auto &value : state.at("bodies"))
		{
			const auto &body = value.at("body");
			const auto id = body.at("id").get<std::uint64_t>();
			const auto label = std::to_string(id) + " / " + body.at("definition").get<std::string>();
			if (ImGui::Selectable(label.c_str(), editor.selected == id))
				editor.selected = id;
		}
		ImGui::End();
		ImGui::Begin("Properties");
		for (const auto &value : state.at("bodies"))
			if (value.at("body").at("id") == editor.selected)
			{
				const auto details = value.dump(2);
				ImGui::TextUnformatted(details.c_str());
			}
		ImGui::End();
		ImGui::Begin("Map");
		ImGui::Text("Committed tick %llu / read only", static_cast<unsigned long long>(app.session->ticks()));
		const auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
		ImGui::InvisibleButton("Live map", ImVec2(std::max(1.f, size.x), std::max(1.f, size.y)),
		                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
		if (ImGui::IsItemHovered())
		{
			auto &io = ImGui::GetIO();
			editor.zoom = std::clamp(editor.zoom * std::pow(1.15f, io.MouseWheel), 0.000001f, 1.f);
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
			{
				editor.pan_x += io.MouseDelta.x;
				editor.pan_y += io.MouseDelta.y;
			}
		}
		auto *draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		for (const auto &value : state.at("bodies"))
		{
			const auto &body = value.at("body");
			const auto id = body.at("id").get<std::uint64_t>();
			ImVec2 point(origin.x + size.x / 2 + editor.pan_x + body.at("position").at("x").get<float>() * editor.zoom,
			             origin.y + size.y / 2 + editor.pan_y - body.at("position").at("y").get<float>() * editor.zoom);
			draw->AddCircleFilled(point, 6,
			                      editor.selected == id ? IM_COL32(255, 220, 100, 255) : IM_COL32(100, 200, 240, 255));
			auto label = std::to_string(id);
			draw->AddText(ImVec2(point.x + 8, point.y), IM_COL32_WHITE, label.c_str());
			const auto mouse = ImGui::GetIO().MousePos;
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			    std::hypot(mouse.x - point.x, mouse.y - point.y) < 12)
				editor.selected = id;
		}
		draw->PopClipRect();
		ImGui::End();
		return;
	}
	ImGui::Begin("Entities");
	for (const auto &instance : editor.document.map()["entities"])
	{
		auto label = std::to_string(instance.at("id").get<std::uint64_t>()) + " / " +
		             instance.at("definition").get<std::string>();
		if (ImGui::Selectable(label.c_str(), editor.selected == instance["id"]))
			editor.selected = instance["id"];
	}
	if (editor.selected)
	{
		if (ImGui::Button("Duplicate"))
			editor.document.duplicate(editor.selected);
		if (ImGui::Button("Delete"))
		{
			editor.document.erase(editor.selected);
			editor.selected = 0;
		}
	}
	ImGui::End();
	ImGui::Begin("Definitions");
	ImGui::InputText("Pack ID", &editor.pack_name);
	if (ImGui::Button("Apply pack ID"))
		attempt(
		    [&]
		    {
			    editor.document.rename_pack(editor.pack_name);
			    editor.clear_textures();
		    });
	if (ImGui::Button("Import PNG"))
		request_file("image");
	if (ImGui::Button("New entity definition"))
	{
		editor.document.create_entity();
		editor.definition = static_cast<int>(editor.document.definitions()["entities"].size() - 1);
		editor.selected = 0;
	}
	auto &definitions = editor.document.definitions()["entities"];
	for (std::size_t i = 0; i < definitions.size(); ++i)
	{
		auto id = definitions[i].at("id").get<std::string>();
		if (ImGui::Selectable(id.c_str(), editor.definition == i))
		{
			editor.definition = static_cast<int>(i);
			editor.selected = 0;
		}
	}
	if (!editor.document.libraries().entities.empty() && ImGui::CollapsingHeader("Dependency entities"))
	{
		for (const auto &[id, entity] : editor.document.libraries().entities)
			if (ImGui::Selectable(id.c_str(), editor.library_parent == id))
				editor.library_parent = id;
		if (!editor.library_parent.empty() && ImGui::Button("Create derived entity"))
			attempt(
			    [&]
			    {
				    editor.document.create_entity(editor.library_parent);
				    editor.definition = static_cast<int>(definitions.size() - 1);
				    editor.selected = 0;
			    });
	}
	ImGui::Separator();
	ImGui::TextWrapped("%s", editor.status.c_str());
	ImGui::End();
	ImGui::Begin("Properties");
	if (auto *instance = selected_instance())
	{
		ImGui::Text("Instance %llu", static_cast<unsigned long long>(editor.selected));
		transform_fields(*instance);
		ImGui::Separator();
		for (std::size_t i = 0; i < definitions.size(); ++i)
			if (definitions[i]["id"] == (*instance)["definition"])
				editor.definition = static_cast<int>(i);
	}
	if (editor.definition >= 0 && editor.definition < static_cast<int>(definitions.size()))
		entity_properties(definitions[editor.definition]);
	else
		ImGui::TextWrapped("Create or select a reusable entity definition.");
	ImGui::End();
	ImGui::Begin("Consoles");
	console_editor();
	ImGui::End();
	ImGui::Begin("Ship slots");
	slots_editor();
	ImGui::End();
	map_view();
	ImGui::Begin("Status");
	ImGui::TextWrapped("%s", editor.status.c_str());
	ImGui::End();
}
} // namespace phoenix
