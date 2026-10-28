module;
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
module phoenix.presentation;

import phoenix.platform.accessibility;
import phoenix.navigation;
namespace phoenix
{
namespace
{
SemanticSnapshot semantic_snapshot(const Application &app)
{
	SemanticSnapshot frame;
	frame.controls = navigation_controls(app);
	frame.owner = app.session;
	frame.generation = app.session ? app.session->generation() : 0;
	frame.status = app.accessible_status;
	frame.settings_label = localise("Settings");
	frame.announce = app.console_surface_active ||
	                 (app.native_menu && (app.screen == Screen::home || app.screen == Screen::server_lobby ||
	                                      app.screen == Screen::client_lobby || app.screen == Screen::settings));
	return frame;
}
void apply_accessibility_actions(Application &app, AccessibilityActions actions)
{
	const auto focuses = [&]
	{
		for (const auto &id : actions.focuses)
			if (app.focus_control)
				app.focus_control(id);
	};
	const auto values = [&]
	{
		for (const auto &[id, value] : actions.values)
			if (app.set_control_value)
				app.set_control_value(id, value);
	};
	if (actions.values_first)
	{
		values();
		focuses();
	}
	else
	{
		focuses();
		values();
	}
	for (const auto &id : actions.actions)
		invoke_navigation(app, id);
}
} // namespace
void publish_semantics(const Application &app)
{
	publish_accessibility(semantic_snapshot(app));
}
void drain_semantic_actions(Application &app)
{
	apply_accessibility_actions(app, take_accessibility_actions(semantic_snapshot(app)));
}
bool probe_native_accessibility(Application &app)
{
	AccessibilityProbe probe;
	probe.snapshot = [&]
	{
		return semantic_snapshot(app);
	};
	probe.focus = [&](const std::string &id)
	{
		if (app.focus_control)
			app.focus_control(id);
	};
	probe.set_value = [&](const std::string &id, const std::string &value)
	{
		return app.set_control_value && app.set_control_value(id, value);
	};
	probe.invoke = [&](const std::string &id)
	{
		return invoke_navigation(app, id);
	};
	probe.at_home = [&]
	{
		return app.screen == Screen::home;
	};
	for (const auto &input : app.screen_inputs)
		probe.editable_ids.push_back(input.id);
	probe.native_menu = app.native_menu;
	return probe_accessibility(probe);
}
} // namespace phoenix
