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
import phoenix.content.controllers;
import phoenix.content.controllers.types;
import phoenix.content.packs;
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
import phoenix.application.preferences;
import phoenix.devices;
#include "surface_state.hpp"
namespace phoenix
{
NavigationFrame ConsoleSurface::State::navigation_frame(Application &app, const ConsoleRegion &region, bool pregame)
{
	return {dom_,
	        *context_,
	        {app.screen_controls, app.screen_readouts, app.screen_inputs, app.screen_toggles, app.control_placements},
	        region,
	        id(""),
	        pregame,
	        app.accessible_status,
	        [this](const std::string &name)
	        {
		        return asset_path(name);
	        },
	        [this](const std::string &action)
	        {
		        return on_screen_markup(action);
	        }};
}
ConversationFrame ConsoleSurface::State::conversation_frame(Application &app, const ConsoleRegion &region)
{
	return {dom_,
	        *context_,
	        {app.screen_controls, app.screen_readouts, app.screen_inputs, app.screen_toggles, app.control_placements},
	        region,
	        authored_resolved_,
	        app.session ? &app.session->catalog() : nullptr,
	        id(""),
	        app.screen == Screen::server_lobby,
	        app.reduced_motion,
	        authored_component_visible("conversation-history"),
	        authored_component_visible("conversation-compose"),
	        authored_component_visible("conversations")};
}
bool ConsoleSurface::State::same_observation_scope(const Observation &left, const Observation &right)
{
	if (left.own.id != right.own.id || left.own.definition != right.own.definition || left.console != right.console ||
	    left.controls != right.controls || left.feeds != right.feeds ||
	    left.additional_consoles.size() != right.additional_consoles.size())
		return false;
	for (std::size_t i = 0; i < left.additional_consoles.size(); ++i)
		if (!same_observation_scope(left.additional_consoles[i], right.additional_consoles[i]))
			return false;
	return true;
}
bool ConsoleSurface::State::same_observation_scope(const std::shared_ptr<const Observation> &left,
                                                   const std::shared_ptr<const Observation> &right)
{
	return left && right ? same_observation_scope(*left, *right) : !left && !right;
}
void ConsoleSurface::State::remember_metadata(const Application &app)
{
	const auto prefix = id("");
	cached_controls_.clear();
	cached_readouts_.clear();
	cached_inputs_.clear();
	cached_toggles_.clear();
	cached_placements_.clear();
	for (const auto &entry : app.screen_controls)
		if (entry.first.starts_with(prefix))
			cached_controls_.push_back(entry);
	for (const auto &entry : app.screen_readouts)
		if (entry.first.starts_with(prefix))
			cached_readouts_.push_back(entry);
	for (const auto &entry : app.screen_inputs)
		if (entry.id.starts_with(prefix))
			cached_inputs_.push_back(entry);
	for (const auto &entry : app.screen_toggles)
		if (entry.first.starts_with(prefix))
			cached_toggles_.insert(entry);
	for (const auto &entry : app.control_placements)
		if (entry.first.starts_with(prefix))
			cached_placements_.insert(entry);
}
std::map<std::string, Application::ControlPlacement> ConsoleSurface::State::interactive_placements() const
{
	std::map<std::string, Application::ControlPlacement> result;
	for (const auto &[name, label] : cached_controls_)
		if (auto found = cached_placements_.find(name); found != cached_placements_.end())
			result.insert(*found);
	for (const auto &input : cached_inputs_)
		if (auto found = cached_placements_.find(input.id); found != cached_placements_.end())
			result.insert(*found);
	return result;
}
void ConsoleSurface::State::check_painted_controls()
{
	const auto current = interactive_placements();
	if (current.size() != painted_controls_.size())
	{
		render_urgent_ = true;
		return;
	}
	for (const auto &[name, placement] : current)
	{
		const auto found = painted_controls_.find(name);
		if (found == painted_controls_.end())
		{
			render_urgent_ = true;
			return;
		}
		const auto &old = found->second;
		if (placement.x != old.x || placement.y != old.y || placement.width != old.width ||
		    placement.height != old.height || placement.enabled != old.enabled)
		{
			render_urgent_ = true;
			return;
		}
	}
}
void ConsoleSurface::State::restore_metadata(Application &app)
{
	app.screen_controls.insert(app.screen_controls.end(), cached_controls_.begin(), cached_controls_.end());
	app.screen_readouts.insert(app.screen_readouts.end(), cached_readouts_.begin(), cached_readouts_.end());
	app.screen_inputs.insert(app.screen_inputs.end(), cached_inputs_.begin(), cached_inputs_.end());
	app.screen_toggles.insert(cached_toggles_.begin(), cached_toggles_.end());
	const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
	for (auto entry : cached_placements_)
	{
		entry.second.focused = entry.first == focused;
		app.control_placements.insert(std::move(entry));
	}
}
std::vector<std::int64_t> ConsoleSurface::State::display_camera(const std::string &widget) const
{
	if (const auto *layout = model_.layout())
		for (const auto &w : layout->at("widgets"))
			if (w.at("id") == widget && w.at("kind") == "navigation")
				return navigation_.display_camera();
	return {0, 0, 1};
}
std::optional<std::string> ConsoleSurface::State::manual_preset(bool automatic) const
{
	if (!model_.observation())
		return {};
	std::optional<std::string> fallback;
	for (const auto &d : model_.observation()->gameplay.value("controls", Json::array()))
		if (d.value("action", "") == "preset" && d.value("enabled", true))
		{
			const auto name = d.value("id", "");
			if (automatic && name == "profile.automatic")
				return name;
			if (!automatic && name.starts_with("profile.") && name != "profile.automatic")
			{
				if (name == manual_profile_)
					return name;
				if (!fallback)
					fallback = name;
			}
		}
	return fallback;
}

// Fit controls after the authored slots have their measured pixel geometry.
// Nested percentage widths can shrink to their intrinsic minimum on a density change.

const std::vector<ConsoleSpec> &ConsoleSurface::State::console_specs(const Application &app, const std::string &ship)
{
	const auto found = resolved_consoles_.find(ship);
	if (found != resolved_consoles_.end())
		return found->second;
	return resolved_consoles_.emplace(ship, resolve_consoles(app.session->catalog(), ship)).first->second;
}
void ConsoleSurface::State::measure_readouts()
{
	visible_readouts_.clear();
	auto *panel = document_->GetElementById("gameplay");
	const auto panel_at = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
	const auto bottom = panel_at.y + panel->GetOffsetHeight() + 100;
	const auto viewport_bottom = static_cast<float>(context_->GetDimensions().y) + 100;
	auto measure = [&](const auto &values)
	{
		for (const auto &[name, text] : values)
			if (auto *target = dom_.element(id(name)); target && target->IsVisible(true))
			{
				const auto at = target->GetAbsoluteOffset(Rml::BoxArea::Border);
				const auto top = name.starts_with("resource/") ? -100.f : std::max(-100.f, panel_at.y - 100);
				const auto limit = name.starts_with("resource/") ? viewport_bottom : std::min(viewport_bottom, bottom);
				if (at.y + target->GetOffsetHeight() >= top && at.y <= limit)
					visible_readouts_.insert(name);
			}
	};
	measure(readout_values_);
	measure(resource_readouts_);
}
bool ConsoleSurface::State::update_readout(const std::string &name, const std::string &text, bool force)
{
	auto *target = dom_.element(id(name));
	if (!target || displayed_values_[name] == text)
		return false;
	// Keep measured offscreen heights; accessible values are published separately.
	if (!force && !displayed_values_[name].empty() && !visible_readouts_.contains(name))
		return false;
	const auto encoded = Rml::StringUtilities::EncodeRml(text);
	if (auto *leaf = target->GetNumChildren() == 1 ? dynamic_cast<Rml::ElementText *>(target->GetChild(0)) : nullptr)
		leaf->SetText(encoded);
	else
		target->SetInnerRML(encoded);
	displayed_values_[name] = text;
	return true;
}

void ConsoleSurface::State::return_authored_components()
{
	// Restore parents before legacy generators replace their children.
	if (!authored_parents_.empty())
		dom_.forget_properties();
	for (const auto &[target, parent_id] : authored_parents_)
	{
		auto *component = authored_component(target);
		auto *parent = document_->GetElementById(parent_id);
		if (component && parent && component->GetParentNode() != parent)
			parent->AppendChild(component->GetParentNode()->RemoveChild(component));
		if (component)
			for (const char *field :
			     {"left", "top", "width", "height", "display", "position", "overflow", "pointer-events", "right",
			      "bottom", "min-width", "min-height", "max-width", "max-height", "box-sizing", "flex"})
				component->RemoveProperty(field);
	}
	authored_parents_.clear();
}
void ConsoleSurface::State::restore_authored_components()
{
	return_authored_components();
	document_->GetElementById("authored-body")->SetInnerRML("");
	authored_dom_.clear();
	authored_program_.reset();
	authored_resolved_ = UiResolved{};
	authored_identity_.clear();
	authored_geometry_.clear();
}
void ConsoleSurface::State::apply_console_theme(const Json *layout)
{
	if (!app_->session)
		return;
	const auto &catalog = app_->session->catalog();
	if (catalog.ui_theme.empty() && (!layout || (!layout->contains("layout") && !layout->contains("theme") &&
	                                             !layout->contains("theme_overrides"))))
	{
		theme_renderer_.reset(*document_);
		active_theme_ = Json::object();
		theme_identity_.clear();
		return;
	}
	const auto theme_name = layout ? layout->value("theme", std::string{}) : std::string{};
	const auto overrides = layout ? layout->value("theme_overrides", Json::object()) : Json::object();
	const auto signature = theme_name + overrides.dump() + std::to_string(generation_);
	if (signature == theme_identity_)
		return;
	theme_identity_ = signature;
	try
	{
		active_theme_ = resolved_ui_theme(catalog, theme_name, overrides);
		theme_renderer_.apply(*document_, catalog, theme_name, overrides);
	}
	catch (const std::exception &error)
	{
		app_->accessible_status = error.what();
	}
}
void ConsoleSurface::State::prepare_console_tabs(const ConsoleRegion &region)
{
	station_pages_.clear();
	if (station_program_owner_.lock() != app_->session)
	{
		station_programs_.clear();
		station_program_owner_ = app_->session;
	}
	std::string markup;
	const auto snapshot = app_->session->console_snapshot(route_);
	if (!snapshot)
		return;
	const auto &catalog = app_->session->catalog();
	const auto specs = resolve_consoles(catalog, snapshot->own.definition);
	for (const auto &tab : model_.available_tabs())
	{
		const Observation *view = snapshot.get();
		for (const auto &extra : snapshot->additional_consoles)
			if (extra.console == tab.id)
				view = &extra;
		const Json *definition = nullptr;
		for (const auto &spec : specs)
			if (spec.id == tab.id)
				definition = &catalog.consoles.at(spec.definition);
		if (!definition || (!definition->value("tab", true) && tab.id != snapshot->console))
			continue;
		auto add = [&](const std::string &group, const std::string &page, const std::string &label,
		               const std::string &component)
		{
			station_pages_.push_back({tab.id, group, page,
			                          (tab.id == snapshot->console ? "" : localise(tab.label) + ": ") +
			                              localise(label) + (tab.attention ? " — " + localise("Unread") : ""),
			                          component, "console-page/" + tab.id + "/" + page});
		};
		if (definition->contains("layout"))
		{
			const auto key = definition->at("id").get<std::string>();
			// Group and layout assets may change without changing the Console definition in Workshop.
			const auto signature = std::to_string(app_->session->generation()) + definition->dump();
			if (!station_programs_.contains(key) || station_programs_.at(key).first != signature)
				station_programs_[key] = {signature, compile_ui_layout(catalog, *definition)};
			auto program = available_ui_layout(station_programs_.at(key).second, console_ui_availability(*view));
			const auto resolved =
			    resolve_console_ui(program, region.width, region.height, text_scale_ / 100.f, authored_pages_, true);
			const auto &root = resolved.root;
			if (root.component.empty() && root.children.empty())
				continue;
			if (root.kind == "tabs")
				for (const auto &page : root.children)
					add(root.id, page.id, page.label, page.component);
			else
				add("", "", tab.label, root.component);
		}
		else
			add("", "", tab.label, "");
	}
	for (const auto &page : station_pages_)
	{
		const auto label = localise(page.label);
		const bool selected = !settings_open_ && model_.observation()->console == page.console &&
		                      (page.group.empty() ||
		                       (authored_pages_.contains(page.group) && authored_pages_.at(page.group) == page.page));
		const auto action = id(page.action);
		markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(action) + "\" class=\"" +
		          (selected ? "selected" : "") + "\">" + Rml::StringUtilities::EncodeRml(label) + "</button>";
		app_->screen_controls.emplace_back(action, label);
		app_->screen_toggles[action] = selected;
	}
	if (!settings_open_)
		markup += authored_tabs_markup_;
	if (markup != console_tabs_markup_)
	{
		dom_.forget_elements();
		document_->GetElementById("console-tabs")->SetInnerRML(markup);
		console_tabs_markup_ = markup;
	}
	for (const auto &page : station_pages_)
		place(*app_, region, id(page.action));
}
void ConsoleSurface::State::build_authored_tree(const UiNode &root)
{
	return_authored_components();
	std::function<std::string(const UiNode &)> markup = [&](const UiNode &node)
	{
		std::string result =
		    "<div id=\"" + Rml::StringUtilities::EncodeRml(layout_node_id(node)) + "\" class=\"layout-node\">";
		for (const auto &child : node.children)
			result += markup(child);
		return result + "</div>";
	};
	document_->GetElementById("authored-body")->SetInnerRML(markup(root));
	if (authored_program_ && authored_program_->header)
	{
		document_->GetElementById("authored-header")->SetInnerRML(markup(*authored_program_->header));
		authored_header_resolved_ =
		    resolve_ui_layout(UiLayout{*authored_program_->header, {}, {}}, {0, 0, 1280, 56}, 1);
	}
	auto placements = authored_resolved_.placements;
	placements.insert(placements.end(), authored_header_resolved_.placements.begin(),
	                  authored_header_resolved_.placements.end());
	for (const auto &placement : placements)
		if (auto *component = authored_component(placement.component))
		{
			authored_parents_[placement.component] = component->GetParentNode()->GetId();
			auto *slot = document_->GetElementById("layout-node/" + placement.id);
			slot->AppendChild(component->GetParentNode()->RemoveChild(component));
			for (const char *field : {"right", "bottom"})
				component->RemoveProperty(field);
		}
}
void ConsoleSurface::State::style_authored_tree(const UiNode &node, const UiNode *parent, bool visible)
{
	auto *box = document_->GetElementById(layout_node_id(node));
	const auto unit = [&](float value)
	{
		return std::to_string(value * text_scale_ / 100.f) + "dp";
	};
	if (node.component == "manual-flight" && !model_.manual_available())
		visible = false;
	dom_.property(box, "display", visible ? "flex" : "none");
	dom_.property(box, "position",
	              !parent || parent->kind == "instruments" || parent->kind == "cutout" ? "absolute" : "relative");
	dom_.property(box, "min-width", unit(node.minimum_width));
	dom_.property(box, "min-height", unit(node.minimum_height));
	dom_.property(box, "flex-direction", node.kind == "row" || node.kind == "wrap" ? "row" : "column");
	dom_.property(box, "flex-wrap", node.kind == "wrap" ? "wrap" : "nowrap");
	dom_.property(box, "overflow", node.scroll ? "auto" : "visible");
	dom_.property(box, "gap", unit(node.gap));
	dom_.property(
	    box, "padding",
	    unit(node.children.empty() || node.kind == "instruments" || node.kind == "cutout" ? 0 : node.padding));
	dom_.property(box, "align-self",
	              node.align == "start" ? "flex-start"
	              : node.align == "end" ? "flex-end"
	                                    : node.align);
	dom_.property(box, "pointer-events", node.component == "manual-flight" ? "none" : "auto");
	if (!parent)
	{
		dom_.property(box, "left", "0dp");
		dom_.property(box, "top", "0dp");
		dom_.property(box, "width", "100%");
		dom_.property(box, "height", "100%");
	}
	else if (parent->kind != "instruments" && parent->kind != "cutout")
	{
		const bool row = parent->kind == "row" || parent->kind == "wrap";
		const auto basis = row ? std::max(node.minimum_width, node.preferred_width)
		                       : std::max(node.minimum_height, node.preferred_height);
		dom_.property(box, "flex", std::to_string(parent->kind == "wrap" ? 0.f : node.stretch) + " 0 " + unit(basis));
		dom_.property(box, "width",
		              parent->kind == "wrap"            ? unit(std::max(node.minimum_width, node.preferred_width))
		              : !row && node.align == "stretch" ? "100%"
		                                                : "auto");
		dom_.property(box, "height",
		              parent->kind == "wrap" ? unit(std::max(node.minimum_height, node.preferred_height)) : "auto");
	}
	if (!node.component.empty())
		if (auto *component = authored_component(node.component))
		{
			dom_.property(component, "display", visible ? "block" : "none");
			dom_.property(component, "flex", "1 1 0dp");
			if (node.aspect <= 0)
			{
				dom_.property(component, "position", "relative");
				dom_.property(component, "left", "0dp");
				dom_.property(component, "top", "0dp");
				dom_.property(component, "width", "100%");
				dom_.property(component, "height", "100%");
			}
			dom_.property(component, "box-sizing", "border-box");
			dom_.property(component, "max-width", "none");
			dom_.property(component, "max-height", "none");
			dom_.property(component, "min-width", "0dp");
			dom_.property(component, "min-height", "0dp");
			dom_.property(component, "overflow", "auto");
			dom_.property(component, "pointer-events", node.component == "manual-flight" ? "none" : "auto");
			for (const auto &[key, value] : node.style.items())
				if (key == "background" || key == "text" || key == "font_family")
					dom_.property(component,
					              key == "background" ? "background-color"
					              : key == "text"     ? "color"
					                                  : "font-family",
					              value.get<std::string>());
			if (node.style.contains("font_size"))
				dom_.property(component, "font-size", unit(node.style.at("font_size").get<float>()));
		}
	for (const auto &child : node.children)
		style_authored_tree(child, &node, visible && (node.kind != "tabs" || authored_pages_.at(node.id) == child.id));
}
void ConsoleSurface::State::place_authored_instruments(const UiNode &node)
{
	auto *box = document_->GetElementById(layout_node_id(node));
	if (node.kind == "instruments" || node.kind == "cutout")
	{
		const float density = context_->GetDensityIndependentPixelRatio();
		const float pad = node.padding * text_scale_ / 100.f;
		const float width = std::max(1.f, box->GetClientWidth() / density - pad * 2),
		            height = std::max(1.f, box->GetClientHeight() / density - pad * 2);
		for (const auto &child : node.children)
		{
			UiRect bounds{width * child.rect.x / 1000, height * child.rect.y / 1000, width * child.rect.width / 1000,
			              height * child.rect.height / 1000};
			if (node.kind == "cutout" && !child.region.empty())
			{
				const float x = width * node.circle_x, y = height * node.circle_y;
				const float radius = std::min(width, height) * node.circle_radius, corner = radius / std::sqrt(2.f);
				const bool right = child.region.ends_with("right"), bottom = child.region.starts_with("bottom");
				bounds = child.region == "centre" ? UiRect{x - radius, y - radius, radius * 2, radius * 2}
				                                  : UiRect{right ? x + corner : 0, bottom ? y + corner : 0,
				                                           right ? width - x - corner : x - corner,
				                                           bottom ? height - y - corner : y - corner};
			}
			auto *slot = document_->GetElementById(layout_node_id(child));
			for (const auto &[field, value] : std::map<std::string, float>{{"left", pad + bounds.x},
			                                                               {"top", pad + bounds.y},
			                                                               {"width", bounds.width},
			                                                               {"height", bounds.height}})
				dom_.property(slot, field, std::to_string(value) + "dp");
		}
	}
	for (const auto &child : node.children)
		place_authored_instruments(child);
}
void ConsoleSurface::State::fit_authored_aspects(const UiNode &node)
{
	if (node.aspect > 0 && !node.component.empty())
		if (auto *component = authored_component(node.component))
		{
			auto *box = document_->GetElementById(layout_node_id(node));
			const float density = context_->GetDensityIndependentPixelRatio();
			const float box_width = box->GetClientWidth() / density, box_height = box->GetClientHeight() / density;
			const auto minimum = text_scale_ / 100.f;
			const float width = std::max({node.minimum_width * minimum, node.minimum_height * minimum * node.aspect,
			                              std::min(box_width, box_height * node.aspect)});
			dom_.property(component, "position", "absolute");
			dom_.property(component, "left", std::to_string((box_width - width) / 2) + "dp");
			dom_.property(component, "top", std::to_string((box_height - width / node.aspect) / 2) + "dp");
			dom_.property(component, "width", std::to_string(width) + "dp");
			dom_.property(component, "height", std::to_string(width / node.aspect) + "dp");
		}
	for (const auto &child : node.children)
		fit_authored_aspects(child);
}
void ConsoleSurface::State::measure_authored_geometry()
{
	if (authored_dom_.empty())
		return;
	const auto origin = document_->GetElementById("authored-body")->GetAbsoluteOffset(Rml::BoxArea::Border);
	authored_resolved_.diagnostics.clear();
	for (auto &placement : authored_resolved_.placements)
		if (auto *component = authored_component(placement.component))
		{
			placement.visible = component->IsVisible(true);
			const auto at = component->GetAbsoluteOffset(Rml::BoxArea::Border);
			placement.bounds = {at.x - origin.x, at.y - origin.y, component->GetOffsetWidth(),
			                    component->GetOffsetHeight()};
			if (placement.visible && (component->GetScrollHeight() > component->GetClientHeight() + 1 ||
			                          component->GetScrollWidth() > component->GetClientWidth() + 1))
				authored_resolved_.diagnostics.push_back(placement.id +
				                                         ": content scrolls to retain usable text and control sizes");
		}
	std::function<void(const UiNode &)> measure = [&](const UiNode &node)
	{
		auto *box = document_->GetElementById(layout_node_id(node));
		const auto at = box->GetAbsoluteOffset(Rml::BoxArea::Border);
		authored_resolved_.containers[node.id] = {at.x - origin.x, at.y - origin.y, box->GetOffsetWidth(),
		                                          box->GetOffsetHeight()};
		for (const auto &child : node.children)
			measure(child);
	};
	measure(authored_resolved_.root);
}
void ConsoleSurface::State::prepare_authored_layout(const Json *layout, const ConsoleRegion &region)
{
	ensure_station_components();
	const bool authored = layout && layout->contains("layout") && navigation_.outcome().empty();
	document_->SetClass("authored", authored);
	apply_console_theme(layout);
	if (!authored)
	{
		prepare_console_tabs(region);
		if (!navigation_.outcome().empty())
		{
			restore_authored_components();
			dom_.property(document_->GetElementById("gameplay"), "display", "none");
			dom_.property(document_->GetElementById("widgets"), "display", "none");
			dom_.property(document_->GetElementById("navigation-layer"), "display", "block");
		}
		return;
	}
	const auto signature = std::to_string(layout_identity_) + "/" + tactical_.console();
	if (authored_identity_ != signature)
	{
		auto compiled = compile_ui_layout(app_->session->catalog(), *layout);
		authored_targets_.clear();
		std::function<void(const UiNode &)> collect = [&](const UiNode &node)
		{
			if (!node.component.empty())
				authored_targets_.insert(node.component);
			for (const auto &child : node.children)
				collect(child);
		};
		collect(compiled.root);
		authored_program_ = available_ui_layout(std::move(compiled), model_.ui_availability());

		authored_identity_ = signature;
	}
	auto geometry_key = [&]
	{
		std::string key = signature + "/" + std::to_string(region.width) + "/" + std::to_string(region.height) + "/" +
		                  std::to_string(text_scale_) + "/" + std::to_string(model_.available_tabs().size());
		for (const auto &[group, page] : authored_pages_)
			key += "/" + group + "/" + page;
		return key;
	};
	const auto geometry = geometry_key();
	if (geometry != authored_geometry_)
	{
		if (!authored_geometry_.empty())
			cancel_pointer();
		authored_resolved_ = resolve_console_ui(*authored_program_, region.width, region.height, text_scale_ / 100.f,
		                                        authored_pages_, model_.available_tabs().size() > 1);
		for (const auto &[group, page] : authored_resolved_.selected_pages)
			authored_pages_[group] = page;
		authored_geometry_ = geometry_key();
	}
	const auto &resolved = authored_resolved_;

	dom_.property(document_->GetElementById("authored-body"), "top", std::to_string(resolved.body.y) + "dp");
	dom_.property(document_->GetElementById("layout-tabs"), "top",
	              std::to_string(resolved.body.y - 48 * text_scale_ / 100.f) + "dp");
	dom_.property(document_->GetElementById("layout-tabs"), "height", std::to_string(48 * text_scale_ / 100.f) + "dp");
	dom_.property(document_->GetElementById("console-tabs"), "top", "0dp");
	dom_.property(document_->GetElementById("console-tabs"), "height", std::to_string(48 * text_scale_ / 100.f) + "dp");
	auto *host = document_->GetElementById("authored-body");
	// Only hide roots not used as targets: avoid toggling a live root twice per frame.
	std::set<Rml::Element *> targets;
	for (const auto &placement : resolved.placements)
		if (auto *component = authored_component(placement.component))
			targets.insert(component);
	for (const char *panel :
	     {"widgets", "gameplay", "comms-layer", "sensor-panel", "navigation-layer", "manual-flight", "power-panel",
	      "repair-panel", "operations-panel", "docking-panel", "craft-panel", "personnel-panel", "away-panel"})
		if (auto *element = document_->GetElementById(panel); element && !targets.contains(element))
			dom_.property(element, "display", "none");
	authored_page_groups_ = resolved.page_groups;
	const auto dom = signature + (resolved.compact ? "/compact" : "/wide");
	if (authored_dom_ != dom)
	{
		build_authored_tree(resolved.root);
		authored_dom_ = dom;
	}
	auto stage_started = GetTime();
	auto stage = [&](const char *name)
	{
		const auto now = GetTime();
		app_->frame_metrics["console/" + std::to_string(route_) + "/" + name] = now - stage_started;
		stage_started = now;
	};
	if (authored_program_->header)
	{
		auto *header = document_->GetElementById("authored-header");
		dom_.property(header, "height", std::to_string(resolved.body.y) + "dp");
		style_authored_tree(*authored_program_->header);
		dom_.property(document_->GetElementById("console-tabs"), "text-align", "right");
		dom_.property(document_->GetElementById("console-tabs"), "overflow", "hidden");
		context_->Update();
		const auto measured =
		    std::max(resolved.body.y, header->GetOffsetHeight() / context_->GetDensityIndependentPixelRatio());
		dom_.property(document_->GetElementById("authored-body"), "top", std::to_string(measured) + "dp");
	}
	style_authored_tree(resolved.root);
	for (const auto &target : authored_targets_)
		if (auto *component = authored_component(target); component && !targets.contains(component))
			dom_.property(component, "display", "none");
	stage("authored-style");
	context_->Update();
	stage("authored-update-style");
	place_authored_instruments(resolved.root);
	stage("authored-instruments");
	context_->Update();
	stage("authored-update-instruments");
	fit_authored_aspects(resolved.root);
	stage("authored-aspects");
	std::string markup;
	for (const auto &tab : resolved.pages)
	{
		if (resolved.page_groups.at(tab.id) == resolved.root.id)
			continue;
		bool unread = false;
		for (const auto &p : resolved.placements)
			if ((p.id == tab.id || p.id.starts_with(tab.id + ".")) &&
			    (p.component == "conversations" || p.component.starts_with("conversation-")))
				for (const auto &thread : model_.conversations().value("threads", Json::array()))
					unread |= thread.value("unread", false);
		const auto label = localise(tab.label) + (unread ? " — " + localise("Unread") : "");
		const bool selected = authored_pages_.at(resolved.page_groups.at(tab.id)) == tab.id;
		markup += "<button id=\"" + id("layout-page/" + tab.id) + "\" class=\"" + (selected ? "selected" : "") + "\">" +
		          Rml::StringUtilities::EncodeRml(label) + "</button>";
		app_->screen_controls.emplace_back(id("layout-page/" + tab.id), label);
		app_->screen_toggles[id("layout-page/" + tab.id)] = selected;
	}
	if (markup != authored_tabs_markup_)
	{
		authored_tabs_markup_ = markup;
	}
	prepare_console_tabs(region);
	stage("authored-tabs");
	context_->Update();
	stage("authored-update-aspects");
	measure_authored_geometry();
	if (authored_program_->header)
	{
		authored_header_resolved_.body = {0, 0, region.width, resolved.body.y};
		std::function<void(const UiNode &)> measure_header = [&](const UiNode &node)
		{
			if (auto *box = document_->GetElementById(layout_node_id(node)))
			{
				const auto at = box->GetAbsoluteOffset(Rml::BoxArea::Border);
				authored_header_resolved_.containers[node.id] = {at.x, at.y, box->GetOffsetWidth(),
				                                                 box->GetOffsetHeight()};
			}
			for (const auto &child : node.children)
				measure_header(child);
		};
		measure_header(*authored_program_->header);

		for (auto &placement : authored_header_resolved_.placements)
			if (auto *component = authored_component(placement.component))
			{
				const auto at = component->GetAbsoluteOffset(Rml::BoxArea::Border);
				placement.visible = component->IsVisible(true);
				placement.bounds = {at.x, at.y, component->GetOffsetWidth(), component->GetOffsetHeight()};
			}
	}

	stage("authored-geometry");
	if (auto *focus = context_->GetFocusElement(); focus && !focus->IsVisible(true))
		for (const auto &placement : resolved.placements)
		{
			auto *component = authored_component(placement.component);
			bool contains = false;
			for (auto *ancestor = focus; ancestor; ancestor = ancestor->GetParentNode())
				if (ancestor == component)
					contains = true;
			if (contains)
				for (const auto &tab : resolved.pages)
					if (placement.id == tab.id || placement.id.starts_with(tab.id + "."))
						if (auto *button = document_->GetElementById(
						        id(resolved.page_groups.at(tab.id) == resolved.root.id
						               ? "console-page/" + model_.observation()->console + "/" + tab.id
						               : "layout-page/" + tab.id)))
							button->Focus();
		}
	if (page_focus_pending_)
	{
		if (auto *focus = context_->GetFocusElement(); focus && !focus->IsVisible(true))
			for (const auto &[group, page] : authored_pages_)
				if (auto *button = document_->GetElementById(id("layout-page/" + page)))
				{
					button->Focus();
					break;
				}
		page_focus_pending_ = false;
	}
	for (const auto &tab : resolved.pages)
		place(*app_, region, id("layout-page/" + tab.id));
}
std::string ConsoleSurface::State::settings_button(const std::string &name, const std::string &label)
{
	local_settings_controls_.emplace_back(id(name), label);
	return "<button id=\"" + id(name) + "\">" + Rml::StringUtilities::EncodeRml(localise(label)) + "</button>";
}
std::string ConsoleSurface::State::star_choice(const std::string &action, const std::string &profile,
                                               const std::map<std::string, int> &levels, const std::string &selected,
                                               bool settings)
{
	const int stars = complexity_stars(levels, profile);
	if (!stars && !levels.empty())
		return {};
	const auto label = (stars ? "Complexity: " + std::to_string(stars) + " of 3 stars" : "Complexity: " + profile) +
	                   (profile == selected ? " (selected)" : "");
	auto markup = settings ? settings_button(action, label) : button(action, label);
	return stars ? complexity_star_button(markup, stars, complexity_stars(levels, selected), asset_path("../menu/"))
	             : markup;
}
bool ConsoleSurface::State::select_lobby_complexity(const std::string &profile)
{
	const auto lobby = app_->session->lobby();
	if (!lobby.active || lobby.launched)
		return false;
	for (const auto &seat : lobby.consoles)
		if (seat.local_screen == route_)
		{
			LobbyCommand command{LobbyAction::select_profile};
			command.slot = seat.slot;
			command.console = seat.id;
			command.profile = profile;
			return app_->session->lobby_command(command);
		}
	return false;
}

bool ConsoleSurface::State::all_stop(const std::string &key)
{
	if (settings_open_ || systems_open_ || model_.manual_active() || !app_ || !game_active(*app_) || !model_.layout())
		return false;
	for (const auto &widget : model_.layout()->at("widgets"))
		if (widget.at("id") == key && widget.at("kind") == "throttle")
		{
			const auto binding = model_.widget_system(key);
			if (model_.system_minimum(binding) > 0 || model_.system_maximum(binding) < 0)
				return false;
			if (!model_.set_speed_fraction(throttles_.zero(model_, key), binding))
				return false;
			std::erase_if(throttle_contacts_,
			              [&](const auto &contact)
			              {
				              return contact.second == key;
			              });
			throttles_.settle(key, throttles_.zero(model_, key), GetTime(), 0);
			app_->accessible_status = localise("All stop");
			return true;
		}
	return false;
}
bool ConsoleSurface::State::support_active() const
{
	return !support_root_.empty();
}
bool ConsoleSurface::State::support_primary() const
{
	return support_active() && model_.observation() && model_.observation()->console == support_root_;
}
bool ConsoleSurface::State::support_repair_control(const std::string &name, const Json &descriptor) const
{
	if (name.starts_with("game.team") || name.starts_with("game.compartment/"))
		return true;
	if (descriptor.value("controller", "").find("repair") != std::string::npos)
		return true;
	const auto consumers = command_consumers(*model_.observation(), descriptor);
	for (const auto &system : model_.observation()->systems)
		if (system.kind == OperatingSystem::Team &&
		    std::find(consumers.begin(), consumers.end(), system.id) != consumers.end())
			return true;
	return false;
}
void ConsoleSurface::State::finish_support_panels(const ConsoleRegion &region)
{
	const bool detail = support_active() && support_detail_ && !settings_open_ && model_.observation() &&
	                    model_.observation()->console == support_navigation_;
	document_->SetClass("support-detail-open", detail);
	auto *close = document_->GetElementById(id("support-close-details"));
	if (!close)
	{
		auto created = document_->CreateElement("button");
		created->SetId(id("support-close-details"));
		close = document_->AppendChild(std::move(created));
		close->SetInnerRML(Rml::StringUtilities::EncodeRml(localise("Close details")));
		close->SetClass("support-detail-close", true);
	}
	dom_.property(close, "display", detail ? "block" : "none");
	auto *body = document_->GetElementById("authored-body");
	dom_.property(body, "left", detail ? "3%" : "0dp");
	dom_.property(body, "right", detail ? "3%" : "0dp");
	dom_.property(body, "bottom", detail ? "3%" : "0dp");
	dom_.property(body, "background-color", detail ? "#102027" : "transparent");
	dom_.property(body, "border", detail ? "2dp #988b71" : "0dp");
	if (detail)
	{
		dom_.property(body, "top", "156dp");
		context_->Update();
		app_->screen_controls.emplace_back(id("support-close-details"), localise("Close details"));
		place(*app_, region, id("support-close-details"));
	}
}
bool ConsoleSurface::State::sharing_hit(const std::string &key, float x, float y) const
{
	const auto found = sharing_pods_.find(key);
	if (found == sharing_pods_.end())
		return false;
	const auto &pod = found->second;
	const float dx = x - pod.x, dy = y - pod.y;
	return CheckCollisionPointRec({x, y}, pod) && std::min(dx, pod.width - dx) + std::min(dy, pod.height - dy) >= 8.f;
}
std::string ConsoleSurface::State::asset_path(const std::string &name) const
{
#ifdef __EMSCRIPTEN__
	return "/assets/helm/" + name;
#else
	return std::string(GetApplicationDirectory()) + "assets/helm/" + name;
#endif
}
Texture2D ConsoleSurface::State::hud(const std::string &name)
{
	if (app_ && app_->session && active_theme_.contains("assets") && active_theme_.at("assets").contains(name))
	{
		const auto ref = active_theme_.at("assets").at(name).get<std::string>();
		return hud_cache_.load(ref, app_->session->catalog().assets.at(ref));
	}
	if (!hud_.contains(name))
	{
		const auto texture = hud_cache_.load_file(asset_path(name + ".png"));
		if (!texture.id)
			throw std::runtime_error("Missing Console artwork: " + name);
		hud_[name] = texture;
	}
	return hud_.at(name);
}
void ConsoleSurface::State::image(Texture2D texture, Rectangle area)
{
	DrawTexturePro(texture, {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)}, area, {0, 0},
	               0, WHITE);
}
Vector2 ConsoleSurface::State::widget_offset(const std::string &key) const
{
	const auto at = document_->GetElementById("widget/" + key)->GetAbsoluteOffset(Rml::BoxArea::Border);
	return {at.x, at.y};
}
Rectangle ConsoleSurface::State::throttle_rect(const std::string &key) const
{
	auto *widget = document_->GetElementById("widget/" + key);
	const auto at = widget_offset(key);
	const auto &size = hud_metadata_.at("helm-throttle-track").at("image_size");
	const float density = context_->GetDensityIndependentPixelRatio();
	Rml::ElementList headings;
	widget->GetElementsByTagName(headings, "h2");
	const float header =
	    std::max(45.f * density, (headings.empty() ? 0.f : headings.front()->GetOffsetHeight()) + 16.f * density);
	const float height = std::max(30.f * density, widget->GetOffsetHeight() - header - 111.f * density),
	            width = height * size[0].get<float>() / size[1].get<float>();
	return {at.x + (widget->GetOffsetWidth() - width) / 2, at.y + header, width, height};
}
void ConsoleSurface::State::place(Application &app, const ConsoleRegion &region, const std::string &name)
{
	if (auto *element = dom_.element(name))
	{
		const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
		app.control_placements[name] = {region.x + at.x,
		                                region.y + at.y,
		                                element->GetOffsetWidth(),
		                                element->GetOffsetHeight(),
		                                context_->GetFocusElement() == element,
		                                !element->HasAttribute("disabled") &&
		                                    !(dynamic_cast<Rml::ElementFormControl *>(element) &&
		                                      dynamic_cast<Rml::ElementFormControl *>(element)->IsDisabled())};
	}
}
std::string ConsoleSurface::State::id(const std::string &action) const
{
	return "console/" + std::to_string(route_) + "/" + action;
}
std::string ConsoleSurface::State::on_screen_markup(const std::string &action) const
{
	return "<button class=\"on-screen\" id=\"" + Rml::StringUtilities::EncodeRml(id(action)) +
	       "\"><span class=\"share-screw share-screw-left\"/><span class=\"share-screw share-screw-right\"/>"
	       "<span class=\"share-monitor\"><span/></span><span class=\"share-label\">" +
	       Rml::StringUtilities::EncodeRml(localise("On screen")) + "</span><span class=\"share-caption share-idle\">" +
	       Rml::StringUtilities::EncodeRml(localise("Standby")) + "</span><span class=\"share-caption share-live\">" +
	       Rml::StringUtilities::EncodeRml(localise("Live")) +
	       "</span><span class=\"share-lamp-housing\"><span class=\"share-lamp\"/></span></button>";
}
std::string ConsoleSurface::State::button(const std::string &action, const std::string &label,
                                          const std::string &visual)
{
	controls_.emplace_back(id(action), label);
	if (action.starts_with("on-screen/"))
		return on_screen_markup(action);
	return "<button " + std::string(action.starts_with("game.contacts/") ? "class=\"radar-contacts\" " : "") +
	       (action.starts_with("camera/") ? "style=\"decorator:none;background-color:transparent\" " : "") + "id=\"" +
	       Rml::StringUtilities::EncodeRml(id(action)) + "\">" +
	       (visual.empty() ? Rml::StringUtilities::EncodeRml(translate(label, app_->pseudo_locale))
	                       : "<span class=\"camera-icon\">" + Rml::StringUtilities::EncodeRml(visual) + "</span>") +
	       "</button>";
}
std::string ConsoleSurface::State::console_markup(std::string markup) const
{
	std::string sheets = complexity_star_styles();
	for (const auto &[token, file] : std::vector<std::pair<std::string, std::string>>{
	         {"CARD_SPRITE", "helm-background-9slice"}, {"BUTTON_SPRITE", "button-generic-9slice"}})
	{
		const auto data = parse_json(read_file(asset_path("../menu/" + file + ".sprite.json"), 65536), file);
		const auto &slice = data.at("nine_slice");
		const int w = data.at("image_size")[0], h = data.at("image_size")[1];
		const int top = slice.at("top"), right = slice.at("right"), bottom = slice.at("bottom"),
		          left = slice.at("left");
		auto px = [](int n)
		{
			return std::to_string(n) + "px ";
		};
		sheets += "@spritesheet " + token + " { src: \"" + asset_path("../menu/" + file + ".png") +
		          "\"; resolution: 1x; " + token + "-outer: 0px 0px " + px(w) + px(h) + "; " + token +
		          "-inner: " + px(left) + px(top) + px(w - left - right) + px(h - top - bottom) + "; }\n";
		const auto needle = "image(" + token + ")";
		markup.replace(markup.find(needle), needle.size(),
		               "ninepatch(" + token + "-outer, " + token + "-inner, 14px 18px 14px 18px) border-box");
	}
	for (auto at = markup.find("px"); at != std::string::npos; at = markup.find("px", at + 2))
		markup.replace(at, 2, "dp");
	markup.insert(markup.find("<style>") + 7, sheets);
	return markup;
}
ConsoleSurface::State::State(ConsoleId route, const std::string &prefix, bool toolbar, Rml::RenderInterface *renderer)
    : route_(route), context_name_(prefix + std::to_string(route)), toolbar_enabled_(toolbar)
{
	terminal_ = parse_json(read_file(asset_path("../menu/menu-scene.json"), 64 * 1024), "menu scene").at("terminal");
	for (int i = 1; i <= 12; ++i)
		crew_portraits_.emplace(sha256(read_file(asset_path("../menu/avatar-" + std::to_string(i) + ".png"), 12288)),
		                        read_file(asset_path("../menu/portrait-" + std::to_string(i) + ".png"), 512 * 1024));
	const auto markup = read_file(asset_path("../console/surface.rml"), 128 * 1024);
	context_ = Rml::CreateContext(context_name_, {1280, 720}, renderer);
	if (!context_)
		throw std::runtime_error("Cannot create independent Console context");
	document_ = context_->LoadDocumentFromMemory(console_markup(std::string(markup.begin(), markup.end())));
	if (!document_)
		throw std::runtime_error("Cannot create Console document");
	dom_.bind(*document_);
	for (const char *event : {"mouseover", "mousemove", "focus", "blur", "keydown", "keyup"})
		document_->AddEventListener(event, this);
	document_->AddEventListener("click", this);
	document_->AddEventListener("mousescroll", this);
	document_->AddEventListener("change", this);
	document_->AddEventListener("mousedown", this);
	document_->AddEventListener("mouseup", this);
	document_->AddEventListener("mouseout", this);
	document_->AddEventListener("touchstart", this);
	document_->AddEventListener("touchend", this);
	document_->AddEventListener("touchcancel", this);
	for (const char *name :
	     {"helm-throttle-track", "helm-throttle-handle", "helm-radar-ring", "helm-background-9slice"})
		hud_metadata_[name] = parse_json(read_file(asset_path(std::string(name) + ".sprite.json"), 64 * 1024), name);
	document_->Show();
}
ConsoleSurface::State::~State()
{
	if (ui_texture_.id)
		UnloadRenderTexture(ui_texture_);
	Rml::RemoveContext(context_name_);
}
void ConsoleSurface::State::preview_text_scale(int scale)
{
	text_scale_ = std::clamp(scale, 50, 200);
}
void ConsoleSurface::State::preview_pages(const Json &layout)
{
	preview_header_ = layout.value("_preview_header", false);
	if (!layout.contains("_preview_pages"))
		return;
	const auto hint = layout.at("_preview_pages").dump();
	const bool interact = layout.value("_preview_interact", false);
	if (hint != preview_pages_signature_ || (preview_interact_ && !interact))
		authored_pages_ = layout.at("_preview_pages").get<std::map<std::string, std::string>>();
	preview_pages_signature_ = hint;
	preview_interact_ = interact;
}
Rml::Context &ConsoleSurface::State::context()
{
	return *context_;
}
void ConsoleSurface::State::update_view()
{
	if (context_->GetNextUpdateDelay() <= 0)
		render_dirty_ = true;
	context_->Update();
	measure_authored_geometry();
	if (settings_open_)
		return;
	// Read geometry as one batch before changing any text. Interleaving these
	// operations forced a complete RmlUi layout for each telemetry readout.
	measure_readouts();
	bool changed = false;
	for (const auto &[name, text] : readout_values_)
		changed |= update_readout(name, text, false);
	for (const auto &[name, text] : resource_readouts_)
		changed |= update_readout(name, text, false);
	if (changed)
	{
		render_dirty_ = true;
		context_->Update();
	}
}
bool ConsoleSurface::State::preparation_urgent() const
{
	return prepare_dirty_ || !prepared_cache_;
}
void ConsoleSurface::State::prepare(Application &app, const ConsoleRegion &physical_region,
                                    const std::set<std::string> &pads, bool allow_fresh)
{
	const auto source = app.session ? app.session->console_snapshot(route_) : nullptr;
	const auto environment = std::to_string(physical_region.x) + "/" + std::to_string(physical_region.y) + "/" +
	                         std::to_string(physical_region.width) + "/" + std::to_string(physical_region.height) +
	                         "/" + app.language + "/" + std::to_string(app.pseudo_locale) + "/" +
	                         std::to_string(text_scale_) + "/" + std::to_string(static_cast<int>(app.screen));
	if (prepared_owner_.lock() != app.session || !app.session || prepared_generation_ != app.session->generation() ||
	    !same_observation_scope(prepared_source_, source))
		render_urgent_ = true;
	const auto record_freshness = [&]
	{
		const auto tick = app.session ? app.session->ticks() : 0;
		app.frame_metrics["console/" + std::to_string(route_) + "/data-age"] =
		    prepared_source_ && tick >= prepared_source_->observed_tick ? (tick - prepared_source_->observed_tick) * .05
		                                                                : 0;
	};
	const bool stable = prepared_cache_ && !prepare_dirty_ && !settings_open_ && source && app.session &&
	                    app.session->running() && app.screen == Screen::console &&
	                    prepared_owner_.lock() == app.session && prepared_generation_ == app.session->generation() &&
	                    prepared_environment_ == environment && prepared_pads_ == pads &&
	                    same_observation_scope(prepared_source_, source);
	if (stable && (source == prepared_source_ || (app.budget_console_preparation && (!allow_fresh || render_dirty_))))
	{
		restore_metadata(app);
		record_freshness();
		return;
	}
	prepare_current(app, physical_region, pads);
	remember_metadata(app);
	check_painted_controls();
	prepared_source_ = app.session ? app.session->console_snapshot(route_) : nullptr;
	prepared_owner_ = app.session;
	prepared_generation_ = app.session ? app.session->generation() : 0;
	prepared_environment_ = environment;
	prepared_pads_ = pads;
	record_freshness();
	prepared_cache_ = true;
	prepare_dirty_ = false;
	render_dirty_ = true;
}
bool ConsoleSurface::State::paint_pending()
{
	if (context_->GetNextUpdateDelay() <= 0)
		render_dirty_ = true;
	if (render_dirty_ && !render_dirty_since_)
		render_dirty_since_ = GetTime();
	return render_dirty_;
}
double ConsoleSurface::State::paint_wait() const
{
	return render_dirty_since_ ? GetTime() - render_dirty_since_ : 0;
}
double ConsoleSurface::State::paint_cost() const
{
	return last_paint_cost_;
}
void ConsoleSurface::State::prepare_loading(const LoadingProgress *loading, const ConsoleRegion &region)
{
	auto *panel = document_->GetElementById("station-loading");
	if (!panel)
	{
		auto node = document_->CreateElement("div");
		node->SetId("station-loading");
		panel = document_->AppendChild(std::move(node));
	}
	station_loading_ = loading != nullptr;
	const auto text =
	    loading ? localise(loading->phase) +
	                  (loading->total ? " " + std::to_string(loading->completed * 100 / loading->total) + "%" : "")
	            : std::string{};
	if (text != station_loading_markup_)
	{
		station_loading_markup_ = text;
		panel->SetInnerRML("<div><h1>" + Rml::StringUtilities::EncodeRml(localise("Loading Console")) +
		                   "</h1><p id=\"" + id("loading") + "\">" + Rml::StringUtilities::EncodeRml(text) +
		                   "</p></div>");
		render_dirty_ = render_urgent_ = true;
	}
	dom_.property(panel, "display", station_loading_ ? "block" : "none");
	if (station_loading_)
	{
		std::erase_if(app_->screen_controls,
		              [&](const auto &row)
		              {
			              return row.first.starts_with(id(""));
		              });
		std::erase_if(app_->screen_readouts,
		              [&](const auto &row)
		              {
			              return row.first.starts_with(id(""));
		              });
		app_->screen_readouts.emplace_back(id("loading"), localise("Loading Console") + ": " + text);
		std::erase_if(app_->screen_inputs,
		              [&](const auto &field)
		              {
			              return field.id.starts_with(id(""));
		              });
		context_->Update();
		place(*app_, region, id("loading"));
	}
}
ConsoleSurface::ConsoleSurface(ConsoleId route, const std::string &prefix, bool toolbar, Rml::RenderInterface *renderer)
    : state_(std::make_unique<State>(route, prefix, toolbar, renderer))
{
}
ConsoleSurface::~ConsoleSurface() = default;
void ConsoleSurface::preview_text_scale(int scale)
{
	state_->preview_text_scale(scale);
}
const UiResolved &ConsoleSurface::authored_geometry() const
{
	return state_->authored_geometry();
}
void ConsoleSurface::preview_pages(const Json &layout)
{
	state_->preview_pages(layout);
}
Rml::Context &ConsoleSurface::context()
{
	return state_->context();
}
bool ConsoleSurface::action(const std::string &name)
{
	return state_->action(name);
}
bool ConsoleSurface::controller_hold(const std::string &name, bool pressed)
{
	return state_->controller_hold(name, pressed);
}
std::string ConsoleSurface::controller_identity() const
{
	return state_->controller_identity();
}
bool ConsoleSurface::controller_flight() const
{
	return state_->controller_flight();
}
Rml::Element *ConsoleSurface::controller_element(const std::string &name)
{
	return state_->controller_element(name);
}
void ConsoleSurface::controller_cancel()
{
	state_->controller_cancel();
}
bool ConsoleSurface::controller_back()
{
	return state_->controller_back();
}
void ConsoleSurface::focus(const std::string &name)
{
	state_->focus(name);
}
bool ConsoleSurface::set_value(const std::string &name, const std::string &text)
{
	return state_->set_value(name, text);
}
void ConsoleSurface::flush_manual()
{
	state_->flush_manual();
}
void ConsoleSurface::cancel_gamepad(const std::string &device)
{
	state_->cancel_gamepad(device);
}
void ConsoleSurface::gamepad(const std::string &device, const std::string &control, float value)
{
	state_->gamepad(device, control, value);
}
void ConsoleSurface::manual_frame(bool keyboard_selected, FlightAxes keyboard_axes, bool focused)
{
	state_->manual_frame(keyboard_selected, keyboard_axes, focused);
}
void ConsoleSurface::prepare_manual(Application &app, const ConsoleRegion &region, bool pregame)
{
	state_->prepare_manual(app, region, pregame);
}
bool ConsoleSurface::pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind)
{
	return state_->pointer(contact, x, y, kind);
}
void ConsoleSurface::cancel_pointer()
{
	state_->cancel_pointer();
}
void ConsoleSurface::update_view()
{
	state_->update_view();
}
bool ConsoleSurface::probe_throttle(unsigned stage)
{
	return state_->probe_throttle(stage);
}
bool ConsoleSurface::probe_conversation() const
{
	return state_->probe_conversation();
}
bool ConsoleSurface::probe_tab(const std::string &action)
{
	return state_->probe_tab(action);
}
bool ConsoleSurface::probe_navigation(unsigned stage)
{
	return state_->probe_navigation(stage);
}
bool ConsoleSurface::probe_fit() const
{
	return state_->probe_fit();
}
bool ConsoleSurface::probe_support_visuals()
{
	return state_->probe_support_visuals();
}
bool ConsoleSurface::probe_readout_scroll()
{
	return state_->probe_readout_scroll();
}
bool ConsoleSurface::probe_report()
{
	return state_->probe_report();
}
bool ConsoleSurface::probe_ready()
{
	return state_->probe_ready();
}
bool ConsoleSurface::probe_on_screen(const std::string &widget)
{
	return state_->probe_on_screen(widget);
}
bool ConsoleSurface::probe_helm(bool verify)
{
	return state_->probe_helm(verify);
}
bool ConsoleSurface::probe_manual(unsigned stage)
{
	return state_->probe_manual(stage);
}
bool ConsoleSurface::probe_engineering(unsigned stage)
{
	return state_->probe_engineering(stage);
}
bool ConsoleSurface::preparation_urgent() const
{
	return state_->preparation_urgent();
}
void ConsoleSurface::prepare(Application &app, const ConsoleRegion &physical_region, const std::set<std::string> &pads,
                             bool allow_fresh)
{
	state_->prepare(app, physical_region, pads, allow_fresh);
}
bool ConsoleSurface::paint_pending()
{
	return state_->paint_pending();
}
double ConsoleSurface::paint_wait() const
{
	return state_->paint_wait();
}
double ConsoleSurface::paint_cost() const
{
	return state_->paint_cost();
}
void ConsoleSurface::render_ui(UiRenderer &renderer, const ConsoleRegion &region, bool allow_paint)
{
	state_->render_ui(renderer, region, allow_paint);
}
void ConsoleSurface::prepare_loading(const LoadingProgress *loading, const ConsoleRegion &region)
{
	state_->prepare_loading(loading, region);
}
void ConsoleSurface::prepare_current(Application &app, const ConsoleRegion &physical_region,
                                     const std::set<std::string> &pads)
{
	state_->prepare_current(app, physical_region, pads);
}
void ConsoleSurface::draw_profile(const ConsoleRegion &region)
{
	state_->draw_profile(region);
}
void ConsoleSurface::draw(const ConsoleRegion &region)
{
	state_->draw(region);
}
OperationsWidgetContext ConsoleSurface::State::operations_context(ConsoleModel *source, const std::string &controller,
                                                                  const std::string &renderer)
{
	return {source ? *source : model_,
	        app_ && app_->session ? &app_->session->catalog() : nullptr,
	        id(""),
	        station_component_scope_,
	        [this](const std::string &action, const std::string &label)
	        {
		        return settings_button(action, label);
	        },
	        [this](const std::string &name)
	        {
		        return asset_path(name);
	        },
	        [this, renderer](const std::string &name)
	        {
		        return renderer.empty() ? station_component_active(name) : renderer == name;
	        },
	        [this](const std::string &name)
	        {
		        return authored_action(name);
	        },
	        controller};
}
} // namespace phoenix
