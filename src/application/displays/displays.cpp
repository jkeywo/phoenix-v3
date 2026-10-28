module;
#include <array>
#include <cstdint>
#include <unordered_map>
#include <charconv>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>
#include <functional>
#include <memory>
#include <string>
#include <vector>

module phoenix.application.displays;
import phoenix.application.preference_store;

namespace phoenix
{
void remember_station_display(Application &app, const std::string &console, int monitor)
{
	if (!app.session)
		return;
	const auto lobby = app.session->lobby();
	std::string own_slot;
	for (const auto &member : lobby.members)
		if (member.id == lobby.self)
			own_slot = member.slot;
	for (const auto &seat : lobby.consoles)
		if (seat.id == console && seat.slot == own_slot)
			for (const auto &slot : lobby.slots)
				if (slot.id == seat.slot)
				{
					auto &saved = app.remembered_station_displays[slot.ship];
					if (monitor < 0)
						saved.erase(console);
					else
						for (const auto &display : app.native_displays)
							if (display.id == monitor)
							{
								const auto key = display.identity.empty() ? display.name : display.identity;
								std::erase_if(saved,
								              [&](const auto &entry)
								              {
									              return entry.second == key;
								              });
								saved[console] = key;
							}
					remember_preferences(app);
					return;
				}
}

bool update_native_displays(Application &app, std::vector<Application::NativeDisplay> displays)
{
	if (displays.empty())
		return false; // Ignore a transient empty enumeration during Windows display reconfiguration.
	auto identity = [](const Application::NativeDisplay &display)
	{
		return display.identity.empty() ? display.name : display.identity;
	};
	bool same = displays.size() == app.native_displays.size();
	for (std::size_t i = 0; same && i < displays.size(); ++i)
		same = displays[i].id == app.native_displays[i].id && displays[i].name == app.native_displays[i].name &&
		       identity(displays[i]) == identity(app.native_displays[i]);
	if (same)
		return false;
	auto remap = [&](int previous)
	{
		for (const auto &old : app.native_displays)
			if (old.id == previous)
				for (const auto &next : displays)
					if (identity(next) == identity(old))
						return next.id;
		return -1;
	};
	const int previous_host = app.host_display;
	int host = remap(previous_host);
	if (host < 0)
	{
		host = displays.front().id;
		for (const auto &display : displays)
			if (display.id == previous_host)
				host = previous_host;
	}
	const auto inspector = remap(app.inspector_display);
	const auto assignments = app.display_consoles;
	const auto names = app.local_player_names;
	const bool lobby = app.session && app.session->lobby().active && !app.session->lobby().launched;
	std::string own_slot;
	if (lobby)
	{
		const auto board = app.session->lobby();
		for (const auto &member : board.members)
			if (member.id == board.self)
				own_slot = member.slot;
	}

	// Release old routes before claiming new ones, including a swap of two monitor indices.
	for (const auto &[monitor, console] : assignments)
		if (lobby && (remap(monitor) != monitor || remap(monitor) == host))
		{
			LobbyCommand release{LobbyAction::assign_local_console};
			release.screen = monitor + 2;
			app.session->lobby_command(release);
		}
	app.display_consoles.clear();
	for (const auto &[monitor, console] : assignments)
		if (remap(monitor) != monitor || remap(monitor) == host)
			app.local_player_names.erase(monitor + 2);
	for (const auto &[monitor, console] : assignments)
	{
		const auto next = remap(monitor);
		if (next == monitor && next != host && next >= 0)
			app.display_consoles[next] = console;
		else
		{
			if (next >= 0 && next != host)
			{
				LobbyCommand claim{LobbyAction::assign_local_console};
				claim.console = console;
				claim.slot = own_slot;
				claim.screen = next + 2;
				if (!lobby || app.session->lobby_command(claim))
				{
					app.display_consoles[next] = console;
					if (names.contains(monitor + 2))
						app.local_player_names[next + 2] = names.at(monitor + 2);
				}
			}
		}
	}
	app.native_displays = std::move(displays);
	app.host_display = host;
	app.inspector_display = inspector == host ? -1 : inspector;
	app.restored_display_session.reset();
	app.restored_display_ship.clear();
	return true;
}

void restore_station_displays(Application &app)
{
	if (!app.can_host || !app.session || app.screen != Screen::server_lobby ||
	    !app.session->preparation_phase().empty())
		return;
	const auto lobby = app.session->lobby();
	if (!lobby.active || lobby.launched)
		return;
	for (const auto &member : lobby.members)
		if (member.id == lobby.self)
			for (const auto &slot : lobby.slots)
				if (slot.id == member.slot)
				{
					if (app.restored_display_session.lock() == app.session && app.restored_display_ship == slot.ship)
						return;
					app.restored_display_session = app.session;
					app.restored_display_ship = slot.ship;
					if (!app.remembered_station_displays.contains(slot.ship))
						return;
					bool changed = false;
					for (const auto &[console, identity] : app.remembered_station_displays.at(slot.ship))
						for (const auto &display : app.native_displays)
							if (display.id != app.host_display && display.id != app.inspector_display &&
							    (display.identity.empty() ? display.name : display.identity) == identity &&
							    !app.display_consoles.contains(display.id))
							{
								LobbyCommand command{LobbyAction::assign_local_console, "", slot.id, "", console};
								command.screen = display.id + 2;
								if (app.session->lobby_command(command))
								{
									app.display_consoles[display.id] = console;
									changed = true;
								}
							}
					if (changed && app.apply_displays)
						app.apply_displays();
					return;
				}
}
} // namespace phoenix
