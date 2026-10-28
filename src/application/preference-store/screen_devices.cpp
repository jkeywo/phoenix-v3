module;
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
module phoenix.application.preference_store;

namespace phoenix
{
std::string screen_identity(const Application &app, ConsoleId route)
{
	const auto found = app.screen_identities.find(route);
	return found == app.screen_identities.end() ? std::string{} : found->second;
}
std::string screen_audio_output(const Application &app, ConsoleId route)
{
	const auto saved = app.screen_devices.find(screen_identity(app, route));
	return saved == app.screen_devices.end() ? "default" : saved->second.audio_output;
}
namespace
{
std::string matching_pad(const Application &app, const ScreenDevices &saved)
{
	std::vector<std::string> matches;
	for (const auto &[id, pad] : app.gamepad_devices)
		if (pad.model == saved.gamepad_model &&
		    (!saved.gamepad_serial.empty() ? pad.serial == saved.gamepad_serial
		                                   : !saved.gamepad_path.empty() && pad.path == saved.gamepad_path))
			matches.push_back(id);
	return matches.size() == 1 ? matches.front() : std::string{};
}
void clear_binding(ScreenDevices &saved)
{
	saved.gamepad_serial.clear();
	saved.gamepad_path.clear();
	saved.gamepad_model.clear();
	saved.gamepad_name.clear();
}
} // namespace
void restore_screen_gamepads(Application &app)
{
	std::map<std::string, ConsoleId> next;
	std::set<std::string> screens;
	std::map<std::string, int> remembered_claims;
	for (const auto &[screen, saved] : app.screen_devices)
		if (const auto device = matching_pad(app, saved); !device.empty())
			++remembered_claims[device];
	for (const auto &[route, screen] : app.screen_identities)
	{
		// Routes 0 and 31 represent the host screen and its optional sidebar.
		if (route == 31 ||
		    (route == 0 && !app.viewscreen_region &&
		     (app.screen == Screen::console ||
		      (app.screen == Screen::settings && app.settings_return == Screen::console))) ||
		    !screens.insert(screen).second)
			continue;
		if (const auto saved = app.screen_devices.find(screen); saved != app.screen_devices.end())
		{
			const auto paired = app.paired_gamepads.find(screen);
			const auto device = paired != app.paired_gamepads.end() && app.gamepad_devices.contains(paired->second)
			                        ? paired->second
			                        : matching_pad(app, saved->second);
			if (!device.empty() &&
			    (remembered_claims[device] == 1 || (paired != app.paired_gamepads.end() && paired->second == device)))
				if (!next.contains(device))
					next[device] = route;
		}
	}
	for (const auto &[device, route] : next)
		if (!app.gamepad_assignments.contains(device) || app.gamepad_assignments.at(device) != route)
			app.gamepad_wait_neutral.insert(device);
	app.gamepad_assignments = std::move(next);
}
bool assign_screen_gamepad(Application &app, ConsoleId route, const std::string &device)
{
	const auto screen = screen_identity(app, route);
	if (screen.empty() || (!device.empty() && !app.gamepad_devices.contains(device)))
		return false;
	auto &saved = app.screen_devices[screen];
	clear_binding(saved);
	app.paired_gamepads.erase(screen);
	if (!device.empty())
	{
		const auto &pad = app.gamepad_devices.at(device);
		for (auto &[other_screen, other] : app.screen_devices)
			if (other_screen != screen &&
			    (matching_pad(app, other) == device ||
			     (app.paired_gamepads.contains(other_screen) && app.paired_gamepads.at(other_screen) == device)))
			{
				clear_binding(other);
				app.paired_gamepads.erase(other_screen);
			}
		app.paired_gamepads[screen] = device;
		saved.gamepad_name = pad.name;
		saved.gamepad_model = pad.model;
		saved.gamepad_serial = pad.serial;
		saved.gamepad_path = pad.path;
	}
	restore_screen_gamepads(app);
	if (!device.empty())
		app.gamepad_wait_neutral.insert(device);
	app.pairing_screen.clear();
	remember_preferences(app);
	return true;
}
bool screen_gamepad_event(Application &app, const std::string &device, const std::string &control, float value)
{
	if (control == "device.neutral")
	{
		if (value == 0)
			app.gamepad_wait_neutral.erase(device);
		return true;
	}
	if (!app.pairing_screen.empty() && value > .5f && control != "flight.yaw" && control != "flight.thrust")
	{
		for (const auto &[route, screen] : app.screen_identities)
			if (screen == app.pairing_screen)
			{
				assign_screen_gamepad(app, route, device);
				break;
			}
		return true;
	}
	if (app.gamepad_wait_neutral.contains(device))
		return true;
	return false;
}
std::vector<std::pair<std::string, std::string>> screen_device_controls(const Application &app, ConsoleId route,
                                                                        const std::string &tab)
{
	std::vector<std::pair<std::string, std::string>> result;
#ifndef __EMSCRIPTEN__
	const auto screen = screen_identity(app, route);
	if (screen.empty())
		return result;
	const auto it = app.screen_devices.find(screen);
	const ScreenDevices saved = it == app.screen_devices.end() ? ScreenDevices{} : it->second;
	if (tab == "audio")
	{
		auto outputs = app.audio_outputs;
		outputs.insert(outputs.begin(), {{"default", localise("System default")}, {"muted", localise("Muted")}});
		for (const auto &[id, name] : outputs)
			result.emplace_back("device.audio/" + id,
			                    (saved.audio_output == id ? localise("Selected") + ": " : "") + name);
		const bool connected = std::any_of(outputs.begin(), outputs.end(),
		                                   [&](const auto &entry)
		                                   {
			                                   return entry.first == saved.audio_output;
		                                   });
		if (!connected)
			result.emplace_back("device.audio/" + saved.audio_output,
			                    localise("Disconnected") + ": " +
			                        (saved.audio_name.empty() ? saved.audio_output : saved.audio_name));
		result.emplace_back("device.test", localise("Test sound"));
	}
	if (tab == "controllers")
	{
		std::string device;
		for (const auto &[id, assigned_route] : app.gamepad_assignments)
			if (screen_identity(app, assigned_route) == screen)
				device = id;

		result.emplace_back("device.pair", localise(app.pairing_screen == screen ? "Press a controller button"
		                                                                         : "Pair by pressing a button"));
		result.emplace_back(
		    "device.unassign",
		    localise("Unassign") + ": " +
		        (saved.gamepad_name.empty() ? localise("No controller") : saved.gamepad_name) +
		        (saved.gamepad_name.empty()
		             ? ""
		             : " / " + localise(device.empty() ? "Disconnected or requires pairing" : "Connected")));
		if (app.pairing_screen == screen)
			result.emplace_back("device.cancel-pair", localise("Cancel pairing"));
		for (const auto &[id, pad] : app.gamepad_devices)
			result.emplace_back("device.pad/" + id,
			                    (device == id ? localise("Selected") + ": " : "") + pad.name + " (" + id + ")");
	}
#endif
	return result;
}
bool screen_device_action(Application &app, ConsoleId route, const std::string &action)
{
#ifndef __EMSCRIPTEN__
	const auto screen = screen_identity(app, route);
	if (screen.empty())
		return false;
	if (action == "device.pair")
		app.pairing_screen = screen;
	else if (action == "device.cancel-pair")
		app.pairing_screen.clear();
	else if (action == "device.unassign")
		return assign_screen_gamepad(app, route, "");
	else if (action.starts_with("device.pad/"))
		return assign_screen_gamepad(app, route, action.substr(11));
	else if (action == "device.test")
	{
		if (app.test_screen_audio)
			app.test_screen_audio(route);
	}
	else if (action.starts_with("device.audio/"))
	{
		const auto output = action.substr(13);
		if (output != "default" && output != "muted" &&
		    std::none_of(app.audio_outputs.begin(), app.audio_outputs.end(),
		                 [&](const auto &entry)
		                 {
			                 return entry.first == output;
		                 }))
			return false;
		app.screen_devices[screen].audio_output = output;
		app.screen_devices[screen].audio_name.clear();
		for (const auto &[id, name] : app.audio_outputs)
			if (id == output)
				app.screen_devices[screen].audio_name = name;
		remember_preferences(app);
	}
	else
		return false;
	return true;
#else
	return false;
#endif
}
} // namespace phoenix
