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
#include "surface_state.hpp"
namespace phoenix
{
void ConsoleSurface::State::prepare_help(const ConsoleRegion &region)
{
	help_level_ =
	    model_.layout() ? console_help_level(*model_.layout(), model_.ui_availability().stars) : Json::object();
	help_page_ = "$console";
	help_label_ = model_.layout() ? localise(model_.layout()->value("name", std::string{})) : std::string{};
	// Resolve the deepest visible selected page, including responsive/nested tabs.
	if (!help_level_.empty())
		for (const auto &page : authored_resolved_.pages)
			if (const auto group = authored_resolved_.page_groups.find(page.id);
			    group != authored_resolved_.page_groups.end() &&
			    authored_resolved_.selected_pages.at(group->second) == page.id &&
			    help_level_.at("tabs").contains(page.id))
			{
				help_page_ = page.id;
				help_label_ = localise(page.label);
			}
	const auto identity =
	    std::to_string(generation_) + "/" + (model_.observation() ? model_.observation()->console : std::string{}) +
	    "/" + model_.ui_availability().profile + "/" + help_page_ + "/" + std::to_string(model_.layout_revision());
	help_identity_ = identity;
	if (systems_open_ && support_popup_ == "help" && (help_open_identity_ != identity || settings_open_))
	{
		systems_open_ = false;
		support_popup_.clear();
	}
	const bool visible = !settings_open_ && !console_help_systems(help_level_, help_page_).empty();
	auto *host = document_->GetElementById("help-control");
	if (!host->GetNumChildren())
		host->SetInnerRML("<button id=\"" + id("help") + "\">?</button>");
	dom_.property(host, "display", visible ? "block" : "none");
	// The actual tab row may be positioned by an authored header.
	auto *tabs = document_->GetElementById("console-tabs");
	const float density = context_->GetDensityIndependentPixelRatio();
	const float top = (tabs->GetAbsoluteOffset().y + tabs->GetOffsetHeight()) / density + 4;
	dom_.property(host, "top", std::to_string(top) + "dp");
	if (visible)
	{
		document_->GetElementById(id("help"))->SetAttribute("aria-label", localise("Help") + ": " + help_label_);
		app_->screen_controls.emplace_back(id("help"), localise("Help") + ": " + help_label_);
		context_->Update();
		place(*app_, region, id("help"));
	}
	dom_.property(document_->GetElementById("help-shade"), "display",
	              systems_open_ && support_popup_ == "help" ? "block" : "none");
}
} // namespace phoenix
