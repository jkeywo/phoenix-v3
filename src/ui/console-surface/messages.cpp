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

std::optional<bool> ConsoleSurface::State::message_action(const std::string &action)
{
	return messages_.action(dom_, action);
}
void ConsoleSurface::State::prepare_messages(Application &app, const ConsoleRegion &region, bool pregame)
{
	const auto observation = model_.observation();
	const auto notice = messages_.refresh(
	    model_.messages(),
	    {dom_,
	     *context_,
	     {app.screen_controls, app.screen_readouts, app.screen_inputs, app.screen_toggles, app.control_placements},
	     region,
	     app.session ? &app.session->catalog() : nullptr,
	     id(""),
	     std::to_string(generation_) + "/" + std::to_string(observation ? observation->own.id : 0),
	     !pregame && !settings_open_ && static_cast<bool>(observation),
	     app.pseudo_locale,
	     GetTime(),
	     app.message_seconds});
	if (!notice.announcement.empty())
		app.accessible_status = notice.announcement;
	if (notice.cue && app.message_cue && !app.message_muted)
		app.message_cue(route_);
}
} // namespace phoenix
