module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.material_preview;
import phoenix.workshop.authoring_core;
import phoenix.content.assets;
import phoenix.content.catalog;
import phoenix.content.documents;
import phoenix.graphics.scene;
import phoenix.workshop.asset_preview;

namespace phoenix
{
struct MaterialPreview::State
{
	SceneRenderer scene;
	RenderTexture2D scene_preview{};
	std::string preview_identity;
	std::string preview_content;
	std::optional<Catalog> preview_catalog;
	std::vector<SceneBody> preview_fields;
	bool preview_fields_ready = false;
	DustConfig preview_dust;
	float preview_speed = 0, preview_heading = 0;
	float preview_x = 0, preview_y = 0;
	int dust_profile = 0;
	float preview_bearing = 90, preview_elevation = 12, preview_distance = 35, preview_thrust = 1;
	int diagnostic = 0;

	bool preview_whole_map = false;
	WorkshopAssetPreview assets;
	void dust_properties(WorkshopDocument &, Json &);
};
MaterialPreview::MaterialPreview() : state_(std::make_unique<State>())
{
}
MaterialPreview::~MaterialPreview()
{
	clear();
}
void MaterialPreview::clear()
{
	state_->assets.clear();
	state_->scene.clear();
	state_->preview_identity.clear();
	state_->preview_content.clear();
	state_->preview_catalog.reset();
	state_->preview_fields.clear();
	state_->preview_fields_ready = false;
	if (state_->scene_preview.id)
		UnloadRenderTexture(state_->scene_preview);
	state_->scene_preview = {};
}
void draw_reference_grid_properties(Json &render)
{
	if (!ImGui::CollapsingHeader(tool_label("Reference grid").c_str()))
		return;
	edit_shared_field(tool_label("Author reference grid").c_str(), render, "grid.enabled");
	const bool authored = render.contains("reference_grid");

	if (!authored)
		return;
	auto &j = render["reference_grid"];
	edit_shared_group(j, "grid");
	edit_shared_field(tool_label("First-person plane height override").c_str(), j, "grid.first_person");
	const bool first_person = j.contains("first_person_plane_y");

	if (first_person)
		edit_shared_field(tool_label("First-person plane height (m)").c_str(), j,
		                  "preview.material.first_person_plane_y");
}
void MaterialPreview::State::dust_properties(WorkshopDocument &document, Json &map)
{
	if (!ImGui::CollapsingHeader(tool_label("Dust motes").c_str()))
		return;
	if (!map.contains("presentation"))
		map["presentation"] = workshop_seed("preview.material.presentation");
	if (!map["presentation"].contains("dust"))
		map["presentation"]["dust"] = workshop_seed("preview.material.dust");
	auto &shared = map["presentation"]["dust"];
	const char *profiles[] = {"Shared", "Native", "Browser"};
	if (ImGui::BeginCombo(tool_label("Dust platform").c_str(), localise(profiles[dust_profile]).c_str()))
	{
		for (int i = 0; i < 3; ++i)
			if (ImGui::Selectable(localise(profiles[i]).c_str(), dust_profile == i))
				dust_profile = i;
		ImGui::EndCombo();
	}
	const std::string platform = dust_profile == 1 ? "native" : dust_profile == 2 ? "web" : "";
	if (dust_profile && tool_button(tool_label("Reset platform overrides").c_str()))
		(void)workshop_custom_action(shared, "dust.reset", {{"platform", platform}});
	edit_shared_group(shared, "dust", {{"platform", platform}});

	for (int i = 0; i < 3; ++i)
	{
		ImGui::PushID(i);
		const Json context{{"platform", platform}, {"index", i}};
		const auto original = workshop_control("dust.texture", shared, context).at("value");
		auto texture = original;
		assets.pick(document, "Mote texture", texture);
		if (tool_button(tool_label("Use bundled texture").c_str()))
			texture = "";
		if (texture != original)
			(void)workshop_set_control(shared, "dust.texture", texture, context);

		ImGui::PopID();
	}
}
void MaterialPreview::draw(WorkshopDocument &document, int definition_index, ToolMessage &status)
{
	auto &preview = *state_;
	auto attempt = [&](auto action)
	{
		try
		{
			action();
		}
		catch (const std::exception &exception)
		{
			status = exception.what();
		}
	};
	if (!tool_window(tool_label("Lighting and sprite preview").c_str()))
	{
		ImGui::End();
		return;
	}
	auto &map = document.map();
	if (!map.contains("lighting"))
		map["lighting"] = workshop_seed("preview.material.lighting");
	auto &light = map["lighting"];
	preview.dust_properties(document, map);
	if (ImGui::CollapsingHeader(tool_label("Scene lighting").c_str()))
	{
		edit_shared_field(tool_label("Sun direction").c_str(), light, "lighting.direction");
		edit_shared_field(tool_label("Sun colour").c_str(), light, "lighting.colour");

		edit_shared_field(tool_label("Sun intensity").c_str(), light, "preview.material.sun_intensity");
		edit_shared_field(tool_label("Ambient fill").c_str(), light, "preview.material.ambient");
		edit_shared_field(tool_label("Hatch spacing").c_str(), light, "preview.material.hatch_spacing");
		edit_shared_field(tool_label("Hatch thickness").c_str(), light, "preview.material.hatch_thickness");
		edit_shared_field(tool_label("Ink strength").c_str(), light, "preview.material.ink_strength");
		edit_shared_field(tool_label("Cast sun shadows").c_str(), light, "lighting.shadows");
		edit_shared_field(tool_label("Shadow bias").c_str(), light, "preview.material.shadow_bias");
		edit_shared_field(tool_label("Shadow softness").c_str(), light, "preview.material.shadow_softness");
	}
	ImGui::Checkbox(tool_label("Preview whole map").c_str(), &preview.preview_whole_map);
	ImGui::SliderFloat(tool_label("Preview bearing").c_str(), &preview.preview_bearing, 0, 359);
	ImGui::SliderFloat(tool_label("Preview elevation").c_str(), &preview.preview_elevation, 5, 80);
	ImGui::SliderFloat(tool_label("Preview distance").c_str(), &preview.preview_distance, 5, 200);
	ImGui::SliderFloat(tool_label("Preview forward thrust").c_str(), &preview.preview_thrust, 0, 1);
	ImGui::DragFloat(tool_label("Preview speed (m/s)").c_str(), &preview.preview_speed, .1f, -1000, 1000);
	ImGui::SliderFloat(tool_label("Preview motion heading").c_str(), &preview.preview_heading, 0, 359);
	ImGui::InputFloat(tool_label("Preview X (m)").c_str(), &preview.preview_x);
	ImGui::InputFloat(tool_label("Preview Y (m)").c_str(), &preview.preview_y);
	if (tool_button(tool_label("Reset preview motion").c_str()))
	{
		preview.preview_x = preview.preview_y = preview.preview_speed = 0;
	}
	const float motion_angle = preview.preview_heading * 3.14159265f / 180;
	preview.preview_x += std::cos(motion_angle) * preview.preview_speed * std::min(GetFrameTime(), .1f);
	preview.preview_y += std::sin(motion_angle) * preview.preview_speed * std::min(GetFrameTime(), .1f);
	const char *modes[] = {"Comic", "Smooth", "Unlit", "Normals", "Depth", "Sun visibility"};
	if (ImGui::BeginCombo(tool_label("Diagnostic view").c_str(), localise(modes[preview.diagnostic]).c_str()))
	{
		for (int i = 0; i < 6; i++)
			if (ImGui::Selectable(localise(modes[i]).c_str(), preview.diagnostic == i))
				preview.diagnostic = i;
		ImGui::EndCombo();
	}
	const auto avail = ImGui::GetContentRegionAvail();
	const int width = std::clamp(static_cast<int>(avail.x), 64, 1600),
	          height = std::clamp(static_cast<int>(avail.y), 64, 1000);
	const auto content_identity = std::to_string(document.revision()) + ":" + map.at("id").get<std::string>();
	const auto identity = content_identity + std::to_string(width) + ":" + std::to_string(height) + ":" +
	                      std::to_string(definition_index) + ":" + std::to_string(preview.preview_whole_map) + ":" +
	                      std::to_string(preview.preview_bearing) + ":" + std::to_string(preview.preview_elevation) +
	                      ":" + std::to_string(preview.preview_distance) + ":" +
	                      std::to_string(preview.preview_thrust) + ":" + std::to_string(preview.diagnostic) + ":" +
	                      std::to_string(preview.preview_speed) + ":" + std::to_string(preview.preview_heading) + ":" +
	                      std::to_string(preview.preview_x) + ":" + std::to_string(preview.preview_y);
	if (identity != preview.preview_identity || preview.preview_whole_map ||
	    (preview.preview_catalog && preview.preview_catalog->base.render_mode == "models"))
		attempt(
		    [&]
		    {
			    if (!preview.preview_catalog || preview.preview_content != content_identity)
			    {
				    preview.preview_catalog = document.verify();
				    preview.preview_fields.clear();
				    preview.preview_fields_ready = false;
				    preview.preview_dust =
				        compile_dust(map.value("presentation", Json::object()).value("dust", Json::object()),
				                     *preview.preview_catalog,
#ifdef __EMSCRIPTEN__
				                     true,
#else
				                     false,
#endif
				                     "presentation.dust");
				    preview.preview_content = content_identity;
				    preview.scene.clear();
			    }
			    const auto &catalog = *preview.preview_catalog;
			    std::vector<SceneBody> bodies;
			    if (preview.preview_whole_map)
			    {
				    for (const auto &instance : catalog.maps.at(map.at("id").get<std::string>()).at("entities"))
				    {
					    SceneBody b;
					    b.id = instance.at("id");
					    b.definition = instance.at("definition");
					    b.x = instance.at("x_mm");
					    b.y = instance.at("y_mm");
					    b.vx = b.vy = 0;
					    b.heading = instance.at("heading_mdeg");
					    const auto &render = catalog.entities.at(b.definition).at("render");
					    if (render.contains("sprite"))
						    b.sprite_entry = sprite_entry_index(
						        compile_sprite(render.at("sprite"), catalog, b.definition),
						        instance.value("sprite_entry", render.value("sprite_entry", std::string{})));
					    b.forward_thrust = static_cast<int>(preview.preview_thrust * 1000);
					    bodies.push_back(std::move(b));
				    }
				    std::uint64_t id = 1000000;
				    for (const auto &slot : map["ship_slots"])
				    {
					    SceneBody b;
					    b.id = id++;
					    b.definition = slot.at("allowed_ships").at(0);
					    b.x = slot.at("x_mm");
					    b.y = slot.at("y_mm");
					    b.vx = b.vy = 0;
					    b.heading = slot.at("heading_mdeg");
					    b.forward_thrust = static_cast<int>(preview.preview_thrust * 1000);
					    bodies.push_back(std::move(b));
				    }
			    }
			    else
			    {
				    const auto &defs = document.definitions()["entities"];
				    if (definition_index >= 0 && definition_index < static_cast<int>(defs.size()))
				    {
					    SceneBody b;
					    b.id = 1;
					    b.definition = defs[definition_index].at("id");
					    b.x = b.y = b.vx = b.vy = b.heading = 0;
					    b.forward_thrust = static_cast<int>(preview.preview_thrust * 1000);
					    const auto &render = catalog.entities.at(b.definition).at("render");
					    if (render.contains("sprite"))
						    b.sprite_entry =
						        sprite_entry_index(compile_sprite(render.at("sprite"), catalog, b.definition),
						                           render.value("sprite_entry", std::string{}));
					    bodies.push_back(std::move(b));
				    }
			    }
			    if (preview.preview_whole_map && catalog.scenarios.contains(map.at("id")))
			    {
				    const auto &regions = catalog.scenarios.at(map.at("id")).regions;
				    if (!preview.preview_fields_ready)
				    {
					    std::set<std::pair<std::int64_t, std::int64_t>> cells;
					    std::int64_t spacing = 0, radius = 0;
					    for (const auto &r : regions)
						    if (r.asteroids)
						    {
							    spacing = r.asteroids->spacing;
							    radius = std::max(radius, r.asteroids->visual_distance);
						    }
					    if (spacing)
					    {
						    for (const auto &b : bodies)
						    {
							    const auto count = radius / spacing + 2;
							    if (count > 256)
								    throw std::runtime_error("Workshop field preview exceeds 512 cells");
							    for (auto x = b.x / spacing - count; x <= b.x / spacing + count; ++x)
								    for (auto y = b.y / spacing - count; y <= b.y / spacing + count; ++y)
								    {
									    cells.emplace(x, y);
									    if (cells.size() > 65536)
										    throw std::runtime_error("Workshop field preview exceeds 65536 cells");
								    }
						    }
						    for (const auto &[x, y] : cells)
							    for (auto generated : generate_asteroid_cell(
							             regions, asteroid_map_seed(map.at("id").get<std::string>()), x, y))
							    {
								    auto &b = generated.scene;
								    const auto &render = catalog.entities.at(b.definition).at("render");
								    b.sprite_entry = sprite_entry_index(
								        compile_sprite(render.at("sprite"), catalog, b.definition),
								        generated.entry.empty() ? render.value("sprite_entry", std::string{})
								                                : generated.entry);
								    preview.preview_fields.push_back(std::move(b));
							    }
					    }
					    preview.preview_fields_ready = true;
				    }
				    bodies.insert(bodies.end(), preview.preview_fields.begin(), preview.preview_fields.end());
			    }
			    SceneEnvironment environment;
			    environment.skybox = map.value("presentation", Json::object()).value("skybox", std::string{});
			    environment.dust = preview.preview_dust;
			    // The selected preview hull is the sole local reference, never every map entity.
			    if (!bodies.empty())
			    {
				    auto reference = std::find_if(bodies.rbegin(), bodies.rend(),
				                                  [](const auto &b)
				                                  {
					                                  return !b.field_instance;
				                                  });
				    auto &own = reference == bodies.rend() ? bodies.front() : *reference;
				    const auto &render = catalog.entities.at(own.definition).at("render");
				    if (render.contains("reference_grid"))
					    environment.grid = compile_reference_grid(render.at("reference_grid"), "render.reference_grid");
				    own.x += static_cast<std::int64_t>(preview.preview_x * 1000);
				    own.y += static_cast<std::int64_t>(preview.preview_y * 1000);
				    own.vx = static_cast<std::int64_t>(std::cos(motion_angle) * preview.preview_speed * 1000);
				    own.vy = static_cast<std::int64_t>(std::sin(motion_angle) * preview.preview_speed * 1000);
				    environment.own = own;
			    }
			    if (!preview.scene_preview.id || preview.scene_preview.texture.width != width ||
			        preview.scene_preview.texture.height != height)
			    {
				    if (preview.scene_preview.id)
					    UnloadRenderTexture(preview.scene_preview);
				    preview.scene_preview = LoadRenderTexture(width, height);
			    }
			    const float bearing = preview.preview_bearing * 3.14159265f / 180,
			                elevation = preview.preview_elevation * 3.14159265f / 180, d = preview.preview_distance;
			    Camera3D camera{{d * std::cos(elevation) * std::cos(bearing), d * std::sin(elevation),
			                     -d * std::cos(elevation) * std::sin(bearing)},
			                    {0, 0, 0},
			                    {0, 1, 0},
			                    50,
			                    CAMERA_PERSPECTIVE};
			    if (environment.own)
			    {
				    const Vector3 centre{environment.own->x / 1000.f, 0, -environment.own->y / 1000.f};
				    camera.position.x += centre.x;
				    camera.position.z += centre.z;
				    camera.target = centre;
			    }
			    BeginTextureMode(preview.scene_preview);
			    ClearBackground({8, 12, 13, 255});
			    try
			    {
				    auto scene_light = light;
				    if (preview.preview_whole_map && catalog.scenarios.contains(map.at("id")))
					    scene_light["environment"] =
					        region_environment(catalog.scenarios.at(map.at("id")).regions, GetTime());
				    preview.scene.draw(catalog, bodies, camera,
				                       {0, 0, static_cast<float>(width), static_cast<float>(height)}, scene_light,
				                       GetTime(), static_cast<SceneDiagnostic>(preview.diagnostic),
				                       {static_cast<float>(width), static_cast<float>(height)}, environment);
			    }
			    catch (...)
			    {
				    EndTextureMode();
				    throw;
			    }
			    EndTextureMode();
			    preview.preview_identity = identity;
		    });
	if (preview.scene_preview.id)
		ImGui::Image(static_cast<ImTextureID>(preview.scene_preview.texture.id),
		             {static_cast<float>(width), static_cast<float>(height)}, {0, 1}, {1, 0});
	ImGui::End();
}

} // namespace phoenix
