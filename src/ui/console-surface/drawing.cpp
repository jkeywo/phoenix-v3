module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.console_surface;
import phoenix.content.assets;
import phoenix.content.controllers.types;
import phoenix.content.ui;

import phoenix.graphics.assets;
import phoenix.graphics.radar;
import phoenix.graphics.scene;
import phoenix.ui.theme;
import phoenix.ui.engineering_widgets;
import phoenix.ui.information_widgets;
import phoenix.ui.conversation_panel;
import phoenix.ui.surface_host;
import phoenix.ui.navigation_panel;
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.application.preferences;
import phoenix.devices;
#include "surface_state.hpp"
namespace phoenix
{

void ConsoleSurface::State::draw_support_shields(float ox, float oy)
{
	if (settings_open_ || systems_open_ || !authored_component_visible("shield-display") || !model_.observation())
		return;
	auto *panel = document_->GetElementById(id("shield-diagram"));
	if (!panel || !panel->IsVisible(true))
		return;
	const auto at = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Vector2 centre{ox + at.x + panel->GetClientWidth() / 2, oy + at.y + panel->GetClientHeight() / 2};
	const float radius = std::min(panel->GetClientWidth(), panel->GetClientHeight()) * .43f;
	if (radius < 12)
		return;
	const auto &view = *model_.observation();
	const auto &catalog = app_->session->catalog();
	if (shield_ship_definition_ != view.own.definition)
	{
		shield_ship_definition_ = view.own.definition;
		shield_ship_sprite_.reset();
		const auto &render = catalog.entities.at(view.own.definition).at("render");
		if (render.contains("sprite"))
		{
			shield_ship_sprite_ = compile_sprite(render.at("sprite"), catalog, "shield ship image");
			const auto &sprite = *shield_ship_sprite_;
			const auto &frame = sprite_frame(sprite, 0, 270);
			auto image = load_asset_image(catalog.assets.at(sprite.albedo));
			auto *pixels = LoadImageColors(image);
			int left = frame.rect[0] + frame.rect[2], top = frame.rect[1] + frame.rect[3], right = -1, bottom = -1;
			if (pixels)
				for (int y = frame.rect[1]; y < frame.rect[1] + frame.rect[3]; ++y)
					for (int x = frame.rect[0]; x < frame.rect[0] + frame.rect[2]; ++x)
						if (pixels[y * image.width + x].a > 16)
						{
							left = std::min(left, x);
							top = std::min(top, y);
							right = std::max(right, x);
							bottom = std::max(bottom, y);
						}
			shield_ship_crop_ =
			    right >= left && bottom >= top
			        ? Rectangle{float(left), float(top), float(right - left + 1), float(bottom - top + 1)}
			        : Rectangle{float(frame.rect[0]), float(frame.rect[1]), float(frame.rect[2]), float(frame.rect[3])};
			if (pixels)
				UnloadImageColors(pixels);
			UnloadImage(image);
		}
	}
	if (shield_ship_sprite_)
	{
		const auto &sprite = *shield_ship_sprite_;
		const auto &frame = sprite_frame(sprite, 0, 270);
		const auto texture = radar_textures_.load(sprite.albedo, catalog.assets.at(sprite.albedo));
		const auto crop = shield_ship_crop_;
		const float scale = radius * 1.35f / std::max(crop.width, crop.height);
		const float w = crop.width * scale, h = crop.height * scale;
		DrawTexturePro(texture, {crop.x, crop.y, crop.width * (frame.mirror_x ? -1.f : 1.f), crop.height},
		               {centre.x, centre.y, w, h}, {w / 2, h / 2}, 0, WHITE);
	}
	else
		draw_radar_icon(RadarIcon::ship, centre, radius * .55f, 0, WHITE);
	for (const auto &shield : shield_sectors_)
	{
		const float bearing = (shield.value("bearing_mdeg", 0LL) - view.own.heading) / 1000.f;
		// Two pools use fore/aft status semicircles rather than a tactical coverage plot.
		const float arc = shield_sectors_.size() == 2 ? 180.f : shield.value("arc_mdeg", 360000LL) / 1000.f;
		const auto capacity = shield.value("capacity", 0LL);
		const float fraction =
		    capacity ? std::clamp(float(double(shield.value("active_strength", 0LL)) / capacity), 0.f, 1.f) : 0;
		const Color colour = !shield.value("enabled", true) ? GRAY
		                     : fraction < .25f              ? RED
		                     : fraction < .6f               ? ORANGE
		                                                    : SKYBLUE;
		const int halves = arc >= 359 ? 2 : 1;
		for (int half = 0; half < halves; ++half)
		{
			const float width = arc / halves, begin = bearing - arc / 2 + width * half + 3, end = begin + width - 6;
			auto stroke = [&](float to, Color tint, float thickness)
			{
				for (int i = 0; i < 96; ++i)
				{
					const float a = (begin + (to - begin) * i / 96) * DEG2RAD,
					            b = (begin + (to - begin) * (i + 1) / 96) * DEG2RAD;
					DrawLineEx({centre.x + std::cos(a) * radius, centre.y - std::sin(a) * radius},
					           {centre.x + std::cos(b) * radius, centre.y - std::sin(b) * radius}, thickness, tint);
				}
			};
			stroke(end, {45, 66, 73, 255}, std::max(6.f, radius * .065f));
			if (fraction > 0)
				stroke(begin + (end - begin) * fraction, colour, std::max(4.f, radius * .045f));
		}
	}
}
void ConsoleSurface::State::render_ui(UiRenderer &renderer, const ConsoleRegion &region, bool allow_paint)
{
	if (app_)
		app_->frame_metrics["console/" + std::to_string(route_) + "/paint-age"] = paint_wait();
	const int width = static_cast<int>(region.width), height = static_cast<int>(region.height);
	if (!ui_texture_.id || ui_texture_.texture.width != width || ui_texture_.texture.height != height)
	{
		if (ui_texture_.id)
			UnloadRenderTexture(ui_texture_);
		ui_texture_ = LoadRenderTexture(width, height);
		render_dirty_ = render_urgent_ = true;
	}
	if (render_dirty_ && (allow_paint || render_urgent_))
	{
		const auto paint_started = GetTime();
		BeginTextureMode(ui_texture_);
		ClearBackground(BLANK);
		renderer.viewport({0, 0, region.width, region.height});
		context_->Render();
		renderer.finish();
		EndTextureMode();
		painted_controls_ = interactive_placements();
		painted_tick_ = prepared_source_ ? prepared_source_->observed_tick : 0;
		render_dirty_ = render_urgent_ = false;
		render_dirty_since_ = 0;
		last_paint_cost_ = GetTime() - paint_started;
	}
	if (app_ && app_->session)
	{
		const auto tick = app_->session->ticks();
		app_->frame_metrics["console/" + std::to_string(route_) + "/visible-data-age"] =
		    tick >= painted_tick_ ? (tick - painted_tick_) * .05 : 0;
	}
	BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
	DrawTexturePro(ui_texture_.texture, {0, 0, float(width), -float(height)},
	               {region.x, region.y, region.width, region.height}, {0, 0}, 0, WHITE);
	EndBlendMode();
}
void ConsoleSurface::State::draw_profile(const ConsoleRegion &region)
{
	if (!settings_open_ && app_->session->lobby().active && !app_->session->lobby().launched && !profile_open_ &&
	    !profile_avatar_.empty())
	{
		const auto key = sha256(profile_avatar_);
		const auto &portrait = crew_portraits_.contains(key) ? crew_portraits_.at(key) : profile_avatar_;
		const auto texture = radar_textures_.load(key + "/portrait", portrait);
		const float density = context_->GetDensityIndependentPixelRatio();
		const float tw = terminal_.value("display_width", 264.f) * density, sw = terminal_.at("size").at(0),
		            scale = tw / sw;
		const auto &screen = terminal_.at("screen");
		const float x = screen.at("x"), y = screen.at("y"), w = screen.at("width"), h = screen.at("height");
		const Rectangle area{region.x + region.width - 20 * density - tw + x * scale, region.y + y * scale, w * scale,
		                     h * .85f * scale};
		const float fill = std::max(area.width / texture.width, area.height / texture.height);
		DrawTexturePro(texture,
		               {(texture.width - area.width / fill) / 2, (texture.height - area.height / fill) / 2,
		                area.width / fill, area.height / fill},
		               area, {0, 0}, 0, WHITE);
	}
}
void ConsoleSurface::State::draw(const ConsoleRegion &region)
{
	const Rectangle entire{region.x, region.y, region.width, region.height};
	BeginScissorMode(static_cast<int>(region.x), static_cast<int>(region.y), static_cast<int>(region.width),
	                 static_cast<int>(region.height));
	const auto background = active_theme_.contains("tokens")
	                            ? active_theme_.at("tokens").value("background", std::string("#080d0f"))
	                            : std::string("#080d0f");
	DrawRectangleRec(entire, GetColor(static_cast<unsigned>(std::stoul(
	                             background.substr(1) + (background.size() == 7 ? "ff" : ""), nullptr, 16))));
	if (active_theme_.contains("assets") && active_theme_.at("assets").contains("background"))
	{
		const auto ref = active_theme_.at("assets").at("background").get<std::string>();
		image(hud_cache_.load(ref, app_->session->catalog().assets.at(ref)), entire);
	}
	if (!(model_.layout() && model_.layout()->contains("layout")))
	{
		const bool command = model_.layout() && model_.layout()->value("command", false);
		if (command || route_ == 31)
		{
			const auto frame = hud("viewscreen-frame");
			// The embedded console shares the viewscreen's left bulkhead. Its top and
			// bottom rails continue behind that joint instead of adding a second border.
			const float overlap = route_ == 31 ? 112.f : 0.f;
			DrawTextureNPatch(frame,
			                  {{0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height)},
			                   112,
			                   112,
			                   112,
			                   112,
			                   NPATCH_NINE_PATCH},
			                  {entire.x - overlap, entire.y, entire.width + overlap, entire.height}, {0, 0}, 0, WHITE);
		}
		else
		{
			const auto background = hud("helm-background-9slice");
			const auto &slices = hud_metadata_.at("helm-background-9slice").at("nine_slice");
			DrawTextureNPatch(background,
			                  {{0, 0, static_cast<float>(background.width), static_cast<float>(background.height)},
			                   slices.at("left").get<int>(),
			                   slices.at("top").get<int>(),
			                   slices.at("right").get<int>(),
			                   slices.at("bottom").get<int>(),
			                   NPATCH_NINE_PATCH},
			                  entire, {0, 0}, 0, WHITE);
		}
	}

	const auto &view = model_.observation();
	const float ox = region.x, oy = region.y;
	std::vector<Rml::Element *> drawn_widgets;
	if (const auto *layout = model_.layout())
		for (const auto &definition : layout->at("widgets"))
			if (auto *widget = document_->GetElementById("widget/" + definition.at("id").get<std::string>());
			    widget && widget->IsVisible(true))
				drawn_widgets.push_back(widget);
	for (auto *widget : drawn_widgets)
	{
		const auto key = widget->GetId().substr(7);
		if (auto *dial = document_->GetElementById("dial/" + key))
		{
			const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
			const float radius = std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f;
			const Vector2 centre{ox + at.x + dial->GetOffsetWidth() / 2, oy + at.y + dial->GetOffsetHeight() / 2};
			if (view && view->feeds & system_bit(OperatingSystem::Radar))
			{
				const auto radar =
				    select_system(view->systems, OperatingSystem::Radar, model_.widget_system(key), false);
				for (const auto &sensor : view->systems)
					if (radar && sensor.id == *radar)
					{
						Json settings = Json::object();
						if (const auto *layout = model_.layout())
							for (const auto &w : layout->at("widgets"))
								if (w.at("id") == key)
								{
									settings = w.value("sweep", Json::object());
									settings["selection_source"] = w.value("selection_source", "sensors");
									settings["selection_tokens"] = active_theme_.value("tokens", Json::object());
									settings["fire_arcs"] = w.value("fire_arcs", false) && model_.fire_arcs_visible();
								}
						settings["contact_arcs"] = model_.contact_arcs_visible();
						settings["sensor_contact"] = model_.sensor_contact();
						auto &sweep = radar_sweeps_[key];
						if (!sweep)
							sweep = std::make_unique<RadarSweep>();
						draw_radar_plot(radar_textures_, app_->session->catalog(), *view, sensor, model_.waypoint(),
						                *sweep, settings, centre, radius * .66f, GetTime(), app_->reduced_motion);
					}
			}
			if (sharing_pods_.contains(key))
			{
				auto pod = sharing_pods_.at(key);
				pod.x += ox;
				pod.y += oy;
				// Two mounting arms join the external pod to the casing, with their roots tucked behind its outer rim.
				for (const auto &[angle, end_x] :
				     std::array<std::pair<float, float>, 2>{{{65.f, pod.x + 25}, {48.f, pod.x + pod.width - 25}}})
				{
					const float radians = angle * 3.141592653589793f / 180;
					const Vector2 start{centre.x + std::cos(radians) * radius * .90f,
					                    centre.y + std::sin(radians) * radius * .90f};
					const Vector2 end{end_x, pod.y + 9};
					const float length = std::hypot(end.x - start.x, end.y - start.y);
					const float rotation = std::atan2(end.y - start.y, end.x - start.x) * 180 / 3.141592653589793f;
					// Reuse the readout's inked ivory rail, including its chipped paint and halftone.
					DrawTexturePro(hud("readout-panel"), {500, 119, 220, 52}, {start.x, start.y, length, 16}, {0, 8},
					               rotation, WHITE);
				}
			}
			image(hud("helm-radar-ring"), {centre.x - radius, centre.y - radius, radius * 2, radius * 2});
			if (sharing_pods_.contains(key))
			{
				const bool active = model_.is_on_screen(key);
				auto pod = sharing_pods_.at(key);
				pod.x += ox;
				pod.y += oy;
				// Use the same illustrated casing as the other live instrument readouts.
				DrawTexturePro(hud("readout-panel"), {24, 80, 2124, 555}, pod, {0, 0}, 0, WHITE);
				const Vector2 lamp{pod.x + pod.width - 31, pod.y + pod.height / 2};
				DrawCircleV(lamp, 12, {7, 12, 10, 255});
				DrawCircleLinesV(lamp, 12, {133, 131, 109, 255});
				DrawCircleV(lamp, 8, active ? Color{107, 252, 130, 255} : Color{42, 65, 43, 255});
				if (active)
					DrawCircleV({lamp.x - 2, lamp.y - 2}, 2, {220, 255, 221, 255});
			}
			if (!model_.manual_active() && dial_systems_.contains(key))
			{
				const auto heading = model_.system_request(dial_systems_.at(key), "heading_mdeg");
				const float angle = heading * 3.141592653589793f / 180000;
				const Vector2 marker{centre.x + std::cos(angle) * radius * .87f,
				                     centre.y - std::sin(angle) * radius * .87f};
				const auto pointer = hud("heading-pointer");
				const float size = std::max(28.f, radius * .2f);
				DrawTexturePro(pointer, {0, 0, static_cast<float>(pointer.width), static_cast<float>(pointer.height)},
				               {marker.x, marker.y, size, size}, {size / 2, size / 2}, 90 - heading / 1000.f, WHITE);
			}
		}
		if (!model_.manual_active() && document_->GetElementById(id("speed/" + key)))
		{
			auto track = throttle_rect(key);
			track.x += ox;
			track.y += oy;
			image(hud("helm-throttle-track"), track);
			const float fraction = static_cast<float>(throttles_.fraction(model_, key));
			const auto &marks = hud_metadata_.at("helm-throttle-track").at("markers");
			const float top = marks.at("travel_max")[1].get<float>() /
			                  hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>(),
			            bottom = marks.at("travel_min")[1].get<float>() /
			                     hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>();
			const float y = track.y + track.height * (bottom - fraction * (bottom - top));
			image(hud("helm-throttle-handle"),
			      {track.x - track.width * .1f, y - track.width * .2f, track.width * 1.2f, track.width * .4f});
			if (context_->GetFocusElement() == document_->GetElementById(id("speed/" + key)))
				DrawRectangleLinesEx(
				    {track.x - track.width * .1f, y - track.width * .2f, track.width * 1.2f, track.width * .4f}, 2,
				    {255, 190, 94, 255});
		}
		if (auto *readout = document_->GetElementById("value/" + key);
		    readout && readout->GetClassNames() == "readout" && !readout->GetInnerRML().empty())
		{
			const auto at = readout->GetAbsoluteOffset(Rml::BoxArea::Border);
			image(hud("readout-panel"),
			      {ox + at.x - 8, oy + at.y - 8, readout->GetOffsetWidth() + 16, readout->GetOffsetHeight() + 16});
		}
	}
	if (auto *focus = context_->GetFocusElement();
	    focus && focus->IsClassSet("radar-contact") && focus->IsVisible(true))
	{
		const auto at = focus->GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto c = focus->GetProperty<Rml::Colourb>("image-color");
		draw_radar_selection({ox + at.x + focus->GetOffsetWidth() / 2, oy + at.y + focus->GetOffsetHeight() / 2}, 16,
		                     {c.red, c.green, c.blue, c.alpha});
	}
	draw_support_shields(ox, oy);
	if (app_ && app_->session)
		navigation_.draw(model_, radar_textures_, app_->session->catalog(),
		                 active_theme_.value("tokens", Json::object()), settings_open_, ox, oy);
	EndScissorMode();
}
} // namespace phoenix
