module;
#include <raylib.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "menu_skin.hpp"
module phoenix.presentation;
import phoenix.files;
#include "menu.hpp"
namespace phoenix
{
namespace
{
std::string escaped(const std::string &value)
{
	std::string result;
	for (const char c : value)
		if (c == '&')
			result += "&amp;";
		else if (c == '<')
			result += "&lt;";
		else if (c == '>')
			result += "&gt;";
		else if (c == '"')
			result += "&quot;";
		else
			result += c;
	return result;
}
} // namespace
struct NativeMenuScreen::State
{
	FileImportInbox imports;
	std::vector<LibraryPack> draft;
	std::string previous, notice, focus_next;
	std::optional<std::uint64_t> save_after;
	bool export_save = false, advanced = false, opened_runtime = false;
	std::weak_ptr<SessionPort> owner;
	Application::MenuBranch shown_branch = Application::MenuBranch::root;
	float column_offset = 0;
	bool button_enabled = true;
	MenuSkin button_skin;
	std::map<std::string, std::function<void()>> actions;
	std::string button(Application &app, const std::string &id, const std::string &label, std::function<void()> action)
	{
		if (button_enabled)
		{
			actions[id] = std::move(action);
			app.screen_controls.emplace_back(id, label);
		}
		using Branch = Application::MenuBranch;
		const auto branch = app.menu_branch;
		const bool selected =
		    (id == "menu.host" && (branch == Branch::host || branch == Branch::scenarios || branch == Branch::ships)) ||
		    (id == "menu.new" && (branch == Branch::scenarios || branch == Branch::ships)) ||
		    (id == "menu.join-ship" && branch == Branch::join_ship) ||
		    (id == "menu.join-player" && branch == Branch::join_player) ||
		    (id == "menu.join-gm" && branch == Branch::join_gm) || id == "menu.scenario/" + app.selected_scenario;
		return "<button id=\"" + escaped(id) + "\" class=\"menu-skinned" + (selected ? " menu-selected" : "") + "\"" +
		       (button_enabled ? "" : " disabled") + ">" +
		       (id == "menu.settings" ? "<span class=\"menu-settings-icon\"/>" : "") +
		       "<span class=\"menu-button-label\">" + escaped(localise(label)) + "</span></button>";
	}
	void back(Application &app)
	{
		if (app.packs_popup)
		{
			app.packs_popup = false;
			focus_next = "menu.packs";
			return;
		}
		if (app.screen == Screen::settings)
		{
			app.screen = app.settings_return;
			focus_next = "menu.settings";
			return;
		}
		if (app.screen != Screen::home)
		{
			app.screen = Screen::home;
			app.menu_branch = Application::MenuBranch::root;
			return;
		}
		using Branch = Application::MenuBranch;
		if (app.menu_branch == Branch::ships)
		{
			app.menu_branch = Branch::scenarios;
			focus_next = "menu.scenario/" + app.selected_scenario;
		}
		else if (app.menu_branch == Branch::scenarios)
		{
			app.menu_branch = Branch::host;
			focus_next = "menu.new";
		}
		else
		{
			app.menu_branch = Branch::root;
			focus_next = "menu.host";
		}
	}
	std::string field(Application &app, const std::string &id, const std::string &label, const std::string &value)
	{
		app.screen_inputs.push_back({id, label, value});
		return "<label for=\"" + id + "\">" + escaped(localise(label)) + "</label><input id=\"" + id +
		       "\" type=\"text\" maxlength=\"1024\"/>";
	}
	void handle_import(Application &app)
	{
		if (auto file = imports.take(); file && file->status != ImportStatus::Cancelled)
		{
			if (!file->error.empty())
				throw std::runtime_error(file->error);
			if (file->purpose == FilePurpose::GamePack)
			{
				app.library->install(file->bytes);
				draft = app.library->packs;
				// Newly installed packs remain disabled until explicitly applied.
				notice = "Pack installed. Enable it and apply the selection.";
			}
			else
			{
				auto candidate = app.new_host();
				candidate->restore(file->bytes);
				app.library->store_save(file->bytes, file->filename, candidate->lobby().scenario);
				app.session = std::move(candidate);
				app.screen = Screen::server_lobby;
				app.can_host = true;
				notice = "Save verified. Ready up to resume.";
			}
		}
		if (save_after && app.session->ticks() > *save_after)
		{
			const auto bytes = app.session->save();
			if (export_save)
				export_file("phoenix-session.zip", bytes);
			else
			{
				app.library->store_save(bytes, "Game at tick " + std::to_string(app.session->ticks()),
				                        app.session->lobby().scenario);
				notice = "Game saved.";
			}
			save_after.reset();
		}
	}
	void poll(Application &app)
	{
		if (!app.native_menu)
			return;
		if (auto result = take_copy_result())
			notice = result->empty() ? "Join Code copied." : *result;
		if (owner.lock() != app.session)
		{
			owner = app.session;
			opened_runtime = false;
			save_after.reset();
			imports.reset();
			notice.clear();
		}
		try
		{
			handle_import(app);
		}
		catch (const std::exception &error)
		{
			notice = error.what();
			save_after.reset();
		}
	}
	bool prepare(Application &app, Rml::ElementDocument &document)
	{
		if (!app.native_menu)
			return false;
		document.GetElementById("lobby")->SetProperty("display", "inline-block");
		document.GetElementById("settings")->SetProperty("display", "inline-block");
		for (const char *id : {"home", "settings", "lobby"})
			document.GetElementById(id)->SetInnerRML(localise(id == std::string("home")       ? "Home"
			                                                  : id == std::string("settings") ? "Settings"
			                                                                                  : "Lobby"));
		if (!document.GetElementById("native-menu"))
		{
			auto element = document.CreateElement("div");
			element->SetId("native-menu");
			document.AppendChild(std::move(element));
		}
		auto *root = document.GetElementById("native-menu");
		for (const char *id : {"host", "join", "view", "edit", "locale"})
			document.GetElementById(id)->SetProperty("display", "none");
		const bool visible = app.screen == Screen::home || app.screen == Screen::server_lobby ||
		                     app.screen == Screen::client_lobby || app.screen == Screen::settings;
		root->SetProperty("display", visible ? "block" : "none");
		for (const char *id : {"app-title", "nav", "heading", "description", "session-controls", "game"})
			document.GetElementById(id)->SetProperty("display", visible ? "none" : "block");
		document.GetElementById("settings-panel")->SetProperty("display", "none");
		if (!visible)
			return false;
		app.screen_controls.clear();
		app.screen_inputs.clear();
		actions.clear();
		auto branch = app.menu_branch;
		const bool modal = app.packs_popup || app.screen == Screen::settings;
		button_enabled = !modal;
		auto add = [&](const std::string &id, const std::string &text, std::function<void()> action)
		{
			return button(app, id, text, std::move(action));
		};
		auto heading = [](const std::string &label)
		{
			return "<h2>" + escaped(localise(label)) + "</h2>";
		};
		auto paragraph = [](const std::string &label)
		{
			return "<p>" + escaped(localise(label)) + "</p>";
		};
		auto readout = [&](const std::string &id, const std::string &label)
		{
			if (!modal)
				app.screen_readouts.emplace_back(id, localise(label));
			return "<p id=\"" + escaped(id) + "\">" + escaped(localise(label)) + "</p>";
		};
		using Branch = Application::MenuBranch;
		std::string markup = "<h1 id=\"menu-title\">PHOENIX / SETTLED DARK</h1>";
		markup += "<div class=\"menu-top\">" +
		          add("menu.settings", "Settings",
		              [&]
		              {
			              app.settings_return = app.screen;
			              app.screen = Screen::settings;
			              focus_next = "menu-volume";
		              }) +
		          "</div>";
		markup += "<div class=\"menu-bottom\">" +
		          add("menu.packs", "Mod Packs",
		              [&]
		              {
			              app.packs_popup = true;
			              draft = app.library->packs;
			              focus_next = "menu.import-pack";
		              }) +
		          "</div>";
		if (app.screen == Screen::home || modal)
		{
			markup += "<div id=\"menu-viewport\"><div id=\"menu-columns\"><div class=\"menu-column\">";
			markup += add("menu.host", "Host Game",
			              [&]
			              {
				              app.menu_branch = Branch::host;
				              app.selected_scenario.clear();
				              focus_next = "menu.new";
			              });
			markup += add("menu.join-ship", "Join Game (New Ship)",
			              [&]
			              {
				              app.menu_branch = Branch::join_ship;
				              focus_next = "join-code";
			              });
			markup += add("menu.join-player", "Join Game (As Player)",
			              [&]
			              {
				              app.menu_branch = Branch::join_player;
				              focus_next = "join-code";
			              });
			markup += add("menu.join-gm", "Join Game (As GM)",
			              [&]
			              {
				              app.menu_branch = Branch::join_gm;
				              focus_next = "join-code";
			              });
			markup += add("menu.workshop", "Workshop",
			              [&]
			              {
				              app.screen = Screen::workshop;
			              });
			markup += add("menu.exit", "Exit",
			              [&]
			              {
				              app.exit_requested = true;
			              });
			if (app.session->lobby().active || app.session->running())
			{
				markup += add("menu.resume", "Return to Game",
				              [&]
				              {
					              app.screen = app.session->running()
					                               ? app.can_host ? Screen::viewscreen : Screen::client_lobby
					                           : app.can_host ? Screen::server_lobby
					                                          : Screen::client_lobby;
				              });
				markup += add("menu.leave", "Leave Game",
				              [&]
				              {
					              app.return_home();
				              });
			}
			markup += "</div>";
			if (branch == Branch::host || branch == Branch::scenarios || branch == Branch::ships)
			{
				markup += "<div class=\"menu-column\">" + add("menu.new", "New Game",
				                                              [&]
				                                              {
					                                              app.menu_branch = Branch::scenarios;
					                                              app.selected_scenario.clear();
					                                              focus_next = "menu.back";
				                                              });
				markup += heading("Saved Games");
				if (app.library->saves.empty())
					markup += paragraph("No saved games yet.");
				for (const auto &save : app.library->saves)
					markup += add("menu.save/" + save.id, save.name + " / " + save.scenario + " / " + save.saved_at,
					              [&, id = save.id]
					              {
						              auto candidate = app.new_host();
						              candidate->restore(app.library->read_save(id));
						              app.session = std::move(candidate);
						              app.can_host = true;
						              app.screen = Screen::server_lobby;
					              });
				markup += add("menu.import-save", "Import Save",
				              [&]
				              {
					              request_file(imports, FilePurpose::GameSave);
				              }) +
				          "</div>";
				if (branch == Branch::scenarios || branch == Branch::ships)
				{
					markup += "<div class=\"menu-column\">" + heading("Scenarios");
					if (app.session->catalog().maps.empty())
						markup += paragraph("Enable mod packs to choose a scenario.");
					for (const auto &[id, scenario] : app.session->catalog().maps)
						markup += add("menu.scenario/" + id, scenario.value("name", id),
						              [&, id]
						              {
							              app.selected_scenario = id;
							              app.menu_branch = Branch::ships;
							              focus_next = "menu.back";
						              });
					markup += "</div>";
				}
				if (branch == Branch::ships && app.session->catalog().maps.contains(app.selected_scenario))
				{
					markup += "<div class=\"menu-column\">";
					for (const auto &slot : app.session->catalog().maps.at(app.selected_scenario).at("ship_slots"))
					{
						const std::string slot_id = slot.at("id");
						markup += heading(slot_id);
						for (const auto &choice : slot.at("allowed_ships"))
						{
							const std::string ship = choice;
							markup += add("menu.ship/" + slot_id + "/" + ship,
							              app.session->catalog().entities.at(ship).value("name", ship),
							              [&, slot_id, ship]
							              {
								              app.session->prepare_lobby(
								                  {app.selected_scenario, {{slot_id, ship}}, slot_id, ""});
								              app.screen = Screen::server_lobby;
								              focus_next = "lobby.ready";
							              });
						}
					}
					markup += "</div>";
				}
			}
			if (branch == Branch::join_ship || branch == Branch::join_player || branch == Branch::join_gm)
			{
				markup += "<div class=\"menu-column connection\">" + heading("Join Game");
				if (!modal)
					markup += field(app, "join-code", "Join Code", app.network_code);
				markup += add("menu.advanced", "Advanced Connection Settings",
				              [&]
				              {
					              advanced = !advanced;
				              });
				if (advanced && !modal)
					markup += field(app, "join-service", "Network service", app.network_service) +
					          field(app, "join-profile", "Local profile", app.network_profile);
				markup += add("menu.connect", "Connect",
				              [&, branch]
				              {
					              if (!app.connect)
						              throw std::runtime_error("Connection is unavailable");
					              app.connect(app.network_code, app.network_service, app.network_profile,
					                          branch == Branch::join_ship ? JoinRole::ship_server
					                          : branch == Branch::join_gm ? JoinRole::gm
					                                                      : JoinRole::player);
				              });
				markup += "</div>";
			}
			markup += "</div></div>";
		}
		else
		{
			const auto lobby = app.session->lobby();
			markup += "<div class=\"lobby-panel\">" + heading(lobby.scenario.empty() ? "Lobby" : lobby.scenario) +
			          readout("menu.join-code", "Join Code: " + lobby.code);
			markup += add("menu.copy-code", "Copy Join Code",
			              [&]
			              {
				              copy_text(app.session->lobby().code);
			              });
			if (!lobby.active && !app.session->running())
			{
				markup += paragraph("Enter a code from Host Game to join a pre-game lobby.");
				markup += add("menu.open-join", "Join Game (As Player)",
				              [&]
				              {
					              app.screen = Screen::home;
					              app.menu_branch = Branch::join_player;
					              focus_next = "join-code";
				              });
			}
			for (const auto &slot : lobby.slots)
				markup += readout(
				    "menu.slot/" + slot.id,
				    slot.id + ": " +
				        (slot.host.empty() ? slot.mandatory ? "AI / " + slot.ship : localise("Empty") : slot.ship));
			for (const auto &member : lobby.members)
			{
				const std::string state = !member.connected  ? "Disconnected"
				                          : !member.approved ? "Awaiting approval"
				                          : !member.verified ? "Verifying content"
				                          : member.ready     ? "Ready"
				                                             : "Not Ready";
				markup += readout("menu.member/" + member.id,
				                  localise(member.name) + " / " + member.slot +
				                      (member.console.empty() ? "" : " / " + member.console) + " / " + localise(state));
			}
			markup += "<p id=\"lobby-countdown\"></p>";
			for (const auto &[id, text] : app.session->lobby_controls())
				markup += add(id, text,
				              [&, id]
				              {
					              app.session->session_action(id);
				              });
			if (app.session->running())
			{
				if (app.can_host)
				{
					markup += add("menu.viewscreen", "Open Viewscreen",
					              [&]
					              {
						              app.screen = Screen::viewscreen;
					              });
					markup += add("menu.save-game", "Save Game",
					              [&]
					              {
						              save_after = app.session->ticks();
						              export_save = false;
						              notice = "Saving at the next committed tick...";
					              });
					markup += add("menu.export-save", "Export Save",
					              [&]
					              {
						              save_after = app.session->ticks();
						              export_save = true;
					              });
				}
				if (app.session->observation())
					markup += add("menu.console", "Open Console",
					              [&]
					              {
						              app.screen = Screen::console;
					              });
				if (!app.session->inspection().is_null())
					markup += add("menu.gm", "Open GM Inspector",
					              [&]
					              {
						              app.screen = Screen::gm;
					              });
			}
			markup += add("menu.leave", "Leave Game",
			              [&]
			              {
				              app.session->lobby_command({LobbyAction::leave});
				              app.return_home();
			              }) +
			          "</div>";
		}
		if (branch != Branch::root || app.screen == Screen::server_lobby || app.screen == Screen::client_lobby)
			markup += "<div class=\"menu-back\">" +
			          add("menu.back", "Back",
			              [&]
			              {
				              back(app);
			              }) +
			          "</div>";
		if (modal)
		{
			button_enabled = true;
			markup += "<div class=\"modal-shade\"><div class=\"menu-dialog\">";
			if (app.screen == Screen::settings)
			{
				markup += heading("Settings") + "<label for=\"menu-volume\">" + escaped(localise("Music volume")) +
				          "</label><input id=\"menu-volume\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"" +
				          "50" + "\"/><p id=\"menu-volume-value\"></p>";
				app.screen_inputs.push_back({"menu-volume", "Music volume", std::to_string(app.music_volume), true});
				markup +=
				    add("menu.language", "Language: " + (app.language == "en" ? std::string("English") : app.language),
				        [&]
				        {
					        const auto languages = installed_languages();
					        auto found = std::find(languages.begin(), languages.end(), app.language);
					        app.language =
					            found == languages.end() || ++found == languages.end() ? languages.front() : *found;
					        remember_preferences(app);
				        });
				markup += add("menu.pseudo", app.pseudo_locale ? "Pseudo-localisation: On" : "Pseudo-localisation: Off",
				              [&]
				              {
					              app.pseudo_locale = !app.pseudo_locale;
					              remember_preferences(app);
				              });
				markup += add("menu.motion", app.reduced_motion ? "Reduced motion: On" : "Reduced motion: Off",
				              [&]
				              {
					              app.reduced_motion = !app.reduced_motion;
					              remember_preferences(app);
				              });
			}
			else
			{
				markup += heading("Mod Packs") + add("menu.import-pack", "Import ZIP Pack",
				                                     [&]
				                                     {
					                                     request_file(imports, FilePurpose::GamePack);
				                                     });
				if (draft.empty())
					markup += paragraph("No installed packs.");
				for (std::size_t i = 0; i < draft.size(); ++i)
				{
					const auto first_control = app.screen_controls.size();
					markup += paragraph(draft[i].id);
					markup += add("menu.pack-toggle/" + draft[i].hash, draft[i].enabled ? "Enabled" : "Disabled",
					              [&, i]
					              {
						              draft[i].enabled = !draft[i].enabled;
					              });
					if (i)
						markup += add("menu.pack-up/" + draft[i].hash, "Move Up",
						              [&, i]
						              {
							              std::swap(draft[i], draft[i - 1]);
						              });
					if (i + 1 < draft.size())
						markup += add("menu.pack-down/" + draft[i].hash, "Move Down",
						              [&, i]
						              {
							              std::swap(draft[i], draft[i + 1]);
						              });
					markup += add("menu.pack-remove/" + draft[i].hash, "Remove from Library",
					              [&, i]
					              {
						              draft.erase(draft.begin() + i);
					              });
					for (std::size_t control = first_control; control < app.screen_controls.size(); ++control)
						app.screen_controls[control].second += " / " + draft[i].id;
				}
				markup += add(
				    "menu.apply-packs", "Apply Pack Selection",
				    [&]
				    {
					    const auto archives = app.library->archives(draft);
					    if (app.session->lobby().active || app.session->running() || app.session->recovery_pending())
						    throw std::runtime_error("Leave the game before applying a different pack selection");
					    const auto old = app.library->archives(app.library->packs);
					    app.session->replace_packs(archives);
					    try
					    {
						    app.library->apply(draft);
					    }
					    catch (...)
					    {
						    app.session->replace_packs(old);
						    throw;
					    }
					    app.selected_scenario.clear();
					    app.menu_branch = Branch::root;
					    notice = "Pack selection verified and applied.";
				    });
			}
			markup += add("menu.close-dialog", "Back",
			              [&]
			              {
				              back(app);
			              }) +
			          "</div></div>";
		}
		markup += "<p id=\"menu-status\"></p>";
		app.screen_action = [this, &app](const std::string &id)
		{
			if (id == "menu.back")
			{
				back(app);
				return true;
			}
			const auto found = actions.find(id);
			if (found == actions.end())
				return false;
			try
			{
				auto callback = found->second;
				actions.clear();
				notice.clear();
				app.status.clear();
				callback();
				return true;
			}
			catch (const std::exception &error)
			{
				notice = error.what();
				return false;
			}
		};
		const auto region =
		    app.viewscreen_region ? *app.viewscreen_region
		    : !app.console_regions.empty()
		        ? app.console_regions.front()
		        : ConsoleRegion{1, 0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
		root->SetProperty("left", std::to_string(region.x) + "px");
		root->SetProperty("top", std::to_string(region.y) + "px");
		root->SetProperty("width", std::to_string(region.width) + "px");
		root->SetProperty("height", std::to_string(region.height) + "px");
		std::string focused;
		if (auto *element = document.GetContext()->GetFocusElement())
			focused = element->GetId();
		if (markup != previous)
		{
			root->SetInnerRML(markup);
			previous = markup;
			if (focus_next.empty())
				focus_next = focused;
		}
		for (const auto &input : app.screen_inputs)
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document.GetElementById(input.id)))
				if (field->GetValue() != input.value)
					field->SetValue(input.value);
		const int columns = branch == Branch::ships       ? 4
		                    : branch == Branch::scenarios ? 3
		                    : branch == Branch::root      ? 1
		                                                  : 2;
		const float column_width =
		    std::min(320.f, std::max(std::min(250.f, region.width - 48.f), (region.width - 48.f) / columns));
		const float target = columns == 1 ? (region.width - column_width + 20.f) / 2 : 24.f;
		column_offset += (target - column_offset) * (app.reduced_motion ? 1.f : std::min(1.f, GetFrameTime() * 18.f));
		if (auto *columns_element = document.GetElementById("menu-columns"))
		{
			columns_element->SetProperty("left", std::to_string(column_offset) + "px");
			columns_element->SetProperty("width", std::to_string(columns * column_width) + "px");
		}
		Rml::ElementList columns_list;
		root->GetElementsByClassName(columns_list, "menu-column");
		for (auto *element : columns_list)
			element->SetProperty("width", std::to_string(column_width) + "px");
		if (branch != shown_branch)
		{
			shown_branch = branch;
			document.GetContext()->Update();
			if (auto *viewport = document.GetElementById("menu-viewport"))
				viewport->SetScrollLeft(std::max(0.f, viewport->GetScrollWidth() - viewport->GetClientWidth()));
		}
		if (!focus_next.empty())
		{
			if (auto *element = document.GetElementById(focus_next))
				element->Focus(true);
			focus_next.clear();
		}
		if (modal && (!document.GetContext()->GetFocusElement() ||
		              document.GetContext()->GetFocusElement()->GetId() == "menu.settings" ||
		              document.GetContext()->GetFocusElement()->GetId() == "menu.packs"))
			if (auto *close = document.GetElementById("menu.close-dialog"))
				close->Focus(true);
		if (auto *countdown = document.GetElementById("lobby-countdown"))
		{
			const auto lobby = app.session->lobby();
			const auto countdown_text =
			    lobby.countdown_ms < 0
			        ? localise(lobby.launched ? "Starting game..." : "Waiting for everyone to ready up.")
			        : localise("Starting in") + " " + std::to_string((lobby.countdown_ms + 999) / 1000);
			countdown->SetInnerRML(escaped(countdown_text));
			app.screen_readouts.emplace_back("lobby-countdown", countdown_text);
		}
		if (auto *value = document.GetElementById("menu-volume-value"))
			value->SetInnerRML(std::to_string(app.music_volume) + "%");
		const auto status = !app.status.empty() ? app.status : !notice.empty() ? notice : app.session->lobby().status;
		document.GetElementById("menu-status")->SetInnerRML(escaped(localise(status)));
		app.accessible_status = localise(status);
		if (!app.screen_readouts.empty())
			app.accessible_status += " " + app.screen_readouts.back().second;
		const auto lobby = app.session->lobby();
		const auto own = std::find_if(lobby.members.begin(), lobby.members.end(),
		                              [&](const auto &member)
		                              {
			                              return member.id == lobby.self;
		                              });
		const bool awaiting_console = own != lobby.members.end() && !own->console.empty() &&
		                              own->role != JoinRole::gm && !app.session->observation();
		if (!awaiting_console && !opened_runtime && lobby.active && lobby.launched && app.session->running() &&
		    (app.screen == Screen::server_lobby || app.screen == Screen::client_lobby))
		{
			opened_runtime = true;
			app.screen = app.can_host ? (app.session->observation() ? Screen::console : Screen::viewscreen)
			                          : (!app.session->inspection().is_null() ? Screen::gm : Screen::console);
		}
		return true;
	}
};
NativeMenuScreen::NativeMenuScreen() : state_(std::make_unique<State>())
{
}
NativeMenuScreen::~NativeMenuScreen() = default;
void NativeMenuScreen::poll(Application &app)
{
	state_->poll(app);
}
bool NativeMenuScreen::prepare(Application &app, Rml::ElementDocument &document)
{
	return state_->prepare(app, document);
}
std::string NativeMenuScreen::style_sheet(const std::string &asset_directory)
{
	auto load = [&](const std::string &file, float scale)
	{
		const auto path = asset_directory + "/menu/";
		auto skin = MenuSkin::parse(parse_json(read_file(path + file, 64 * 1024), file), scale);
		Image image = LoadImage((path + skin.sprite).c_str());
		const bool valid = image.data && image.width == skin.width && image.height == skin.height;
		UnloadImage(image);
		if (!valid)
			throw std::runtime_error("Menu sprite dimensions disagree with sidecar: " + file);
		return skin;
	};
	state_->button_skin = load("button-generic-9slice.sprite.json", .075f);
	if (!state_->button_skin.has_label)
		throw std::runtime_error("Menu button sidecar needs label_center");
	const auto panel = load("helm-background-9slice.sprite.json", .12f);
	std::string styles = state_->button_skin.sprite_sheet("menu-button") + panel.sprite_sheet("menu-panel");
	styles += "#native-menu button.menu-skinned { decorator: " + state_->button_skin.decorator("menu-button") + "; }\n";
	styles += "#native-menu .menu-dialog { decorator: " + panel.decorator("menu-panel") + "; }\n";
	const auto &skin = state_->button_skin;
	styles += "#native-menu button.menu-skinned { padding: " +
	          std::to_string(std::max(skin.top, skin.bottom) * skin.scale + 4) + "px " +
	          std::to_string(std::max(skin.left, skin.right) * skin.scale + 8) + "px; }\n";
	styles += R"(
    #native-menu { decorator:image(menu/background.png cover); color:#fff0d4; }
    #native-menu h1 { color:#fff0d4; font-size:38px; letter-spacing:4px; }
    #native-menu h2 { color:#fff0d4; }
    #native-menu button.menu-skinned { position:relative; min-height:64px; line-height:24px; background:transparent; border:2px transparent; color:#fff4df; text-align:center; image-color:#ffffff; }
    #native-menu button:hover, #native-menu button:focus { image-color:#ffce93; border-color:#ffad54; background:#17120c; }
    #native-menu button:active, #native-menu button.menu-selected { image-color:#ffb668; border-color:#f58330; background:#21170c; }
    #native-menu button:disabled { opacity:0.48; image-color:#b9b3a7; border-color:transparent; background:transparent; color:#ddd4c0; }
    .menu-button-label { display:block; position:relative; pointer-events:none; }
    #native-menu .menu-top { width:184px; }
    #native-menu .menu-top button.menu-skinned { decorator:none; padding:18px 8px 18px 64px; }
    .menu-settings-icon { position:absolute; left:4px; top:4px; width:54px; height:54px; decorator:image(menu/settings.png contain); pointer-events:none; }
    #native-menu .menu-dialog { background:transparent; border:0px; padding:36px; }
    #native-menu .modal-shade { background:#080705dd; }
    #native-menu scrollbarhorizontal, #native-menu scrollbarvertical { background:#1b1915; }
    #native-menu scrollbarhorizontal sliderbar, #native-menu scrollbarvertical sliderbar { background:#c7b895; }
    #native-menu input { background:#211f1a; color:#fff0d4; border-color:#a99875; }
    #menu-volume slidertrack { background:#60513a; }
    #menu-volume sliderbar { background:#e1893c; border-color:#fff0d4; }
    )";
	return styles;
}
void NativeMenuScreen::position_labels(Rml::ElementDocument &document)
{
	auto *root = document.GetElementById("native-menu");
	if (!root)
		return;
	Rml::ElementList labels;
	root->GetElementsByClassName(labels, "menu-button-label");
	for (auto *label : labels)
	{
		auto *button = label->GetParentNode();
		if (button->GetId() == "menu.settings")
			continue;
		const float width = button->GetOffsetWidth(), height = button->GetOffsetHeight();
		const auto anchor = state_->button_skin.label_position(width, height);
		label->SetProperty("left", std::to_string(anchor[0] - width * .5f) + "px");
		label->SetProperty("top", std::to_string(anchor[1] - height * .5f) + "px");
	}
}
} // namespace phoenix
