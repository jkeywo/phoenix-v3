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
bool ConsoleSurface::State::prepare_specialised_component(const std::string &component, const ConsoleRegion &region)
{
	if (settings_open_ || !model_.layout() ||
	    (!model_.layout()->value("strict_widgets", false) && component != "flight-actions") || !model_.observation() ||
	    !authored_component_visible(component))
		return false;
	if (component != "weapons" && component != "missiles" && component != "flight-status" &&
	    component != "lateral-left" && component != "lateral-right" && component != "veil-controls" &&
	    component != "repair" && component != "target-info" && component != "target-actions" &&
	    component != "flight-actions")
		return false;
	SpecialisedPanel rendered;
	if (component == "target-info" || component == "target-actions" || component == "weapons" ||
	    component == "missiles")
		rendered = tactical_.render_specialised(component, model_, app_->session->catalog(), task_sources(), id(""));
	else if (component == "veil-controls" || component == "repair")
		rendered = operations_.render_specialised(component, model_, id(""));
	else
		rendered = render_flight_component(component, model_, id(""), document_->IsClassSet("courier-narrow"));
	auto &markup = rendered.markup;
	auto &readouts = rendered.readouts;
	auto &compact_readouts = rendered.compact_readouts;
	auto &buttons = rendered.buttons;
	for (const auto &[key, action] : rendered.actions)
		if (action.empty())
			task_actions_.erase(key);
		else
			task_actions_[key] = action;
	for (const auto &[key, selected] : rendered.toggles)
		app_->screen_toggles[key] = selected;
	const auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	auto *panel = authored_component(component);
	const auto width = panel->GetClientWidth() / context_->GetDensityIndependentPixelRatio();
	panel->SetClass("compact-weapons", component == "weapons" && width < 220 * text_scale_ / 100.f);
	panel->SetClass("landscape-weapon-grid", (component == "weapons" || component == "missiles") &&
	                                             region.width > region.height && width >= 220 * text_scale_ / 100.f);
	if (station_component_markup_[component] != markup)
	{
		panel->SetInnerRML(markup);
		station_component_markup_[component] = markup;
		dom_.forget_elements();
	}
	for (const auto &[key, value] : readouts)
	{
		if (auto *field = document_->GetElementById(key))
		{
			dom_.leaf_markup(field, esc(compact_readouts.contains(key) ? compact_readouts.at(key) : value));
			field->SetAttribute("aria-label", value);
		}
		app_->screen_readouts.emplace_back(key, value);
	}
	for (const auto &[key, fraction] : rendered.meter_fills)
		dom_.property(document_->GetElementById(key), "width", std::to_string(100.f * fraction) + "%");
	context_->Update();
	for (const auto &[key, value] : readouts)
		place(*app_, region, key);
	for (const auto &[key, label] : buttons)
	{
		app_->screen_controls.emplace_back(key, label);
		place(*app_, region, key);
	}
	return true;
}
} // namespace phoenix
