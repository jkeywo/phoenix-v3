module;
#include <raylib.h>
#include <qrcodegen.hpp>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "menu_skin.hpp"
#include "menu_flash.hpp"
#include "menu_ambient.hpp"
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
	Texture2D join_qr{};
	Texture2D panorama{};
	Texture2D flash_overlay{};
	MenuFlash flash;
	MenuAmbient ambient;
	float scutter_aspect = 1;
	double panorama_phase = 0, panorama_period = 720;
	float terminal_aspect = 1;
	std::string join_qr_url;
	TextureCache avatar_textures;
	std::uint64_t avatar_screen = 0;
	void apply_profile(Application &app)
	{
		if (app.player_name.empty() || app.player_name.size() > 64 ||
		    std::any_of(app.player_name.begin(), app.player_name.end(),
		                [](unsigned char c)
		                {
			                return c < 32 || c == 127;
		                }))
			throw std::runtime_error("Enter a name of up to 64 bytes without control characters");
		verify_player_avatar(app.avatar);
		if (app.session)
			app.session->set_player_profile(app.player_name, app.avatar);
		if (app.library)
		{
			app.library->preferences.player_name = app.player_name;
			app.library->preferences.avatar = app.avatar;
			app.library->remember();
		}
		notice = "Profile updated.";
	}
	~State()
	{
		if (join_qr.id)
			UnloadTexture(join_qr);
		if (panorama.id)
			UnloadTexture(panorama);
		if (flash_overlay.id)
			UnloadTexture(flash_overlay);
	}
	void prepare_qr(const Application &app, const std::string &code)
	{
		auto encode = [](const std::string &value)
		{
			std::string result;
			const char *digits = "0123456789ABCDEF";
			for (unsigned char c : value)
			{
				if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
				    c == '_' || c == '.')
					result += c;
				else
				{
					result += '%';
					result += digits[c >> 4];
					result += digits[c & 15];
				}
			}
			return result;
		};
		const auto page = app.join_page.empty() ? "https://jkeywo.github.io/phoenix-v3/" : app.join_page;
		const auto url = page + "?role=player&code=" + encode(code) + "&service=" + encode(app.network_service);
		if (url == join_qr_url)
			return;
		const auto qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
		// Keep one texel per module; the CRT draws an integer number of pixels per module.
		const int scale = 1;
		auto image = GenImageColor((qr.getSize() + 8) * scale, (qr.getSize() + 8) * scale, WHITE);
		for (int y = 0; y < qr.getSize(); ++y)
			for (int x = 0; x < qr.getSize(); ++x)
				if (qr.getModule(x, y))
					ImageDrawRectangle(&image, (x + 4) * scale, (y + 4) * scale, scale, scale, BLACK);
		if (join_qr.id)
			UnloadTexture(join_qr);
		join_qr = LoadTextureFromImage(image);
		UnloadImage(image);
		join_qr_url = url;
	}
	void choose_scenario(Application &app, std::string id)
	{
		const auto &slots = app.session->catalog().maps.at(id).at("ship_slots");
		if (slots.size() == 1 && slots.front().at("allowed_ships").size() == 1)
		{
			const std::string slot = slots.front().at("id"), ship = slots.front().at("allowed_ships").front();
			app.session->prepare_lobby({id, {{slot, ship}}, slot, ""});
			app.screen = Screen::server_lobby;
			focus_next = "lobby.ready";
		}
		else
			focus_next = "menu.back";
		app.selected_scenario = id;
		app.menu_branch = Application::MenuBranch::ships;
	}
	void new_game(Application &app)
	{
		const auto &maps = app.session->catalog().maps;
		if (maps.size() == 1)
			choose_scenario(app, maps.begin()->first);
		else
		{
			app.menu_branch = Application::MenuBranch::scenarios;
			app.selected_scenario.clear();
			focus_next = "menu.back";
		}
	}
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
			focus_next =
			    app.screen == Screen::home || app.screen == Screen::server_lobby || app.screen == Screen::client_lobby
			        ? "menu.settings"
			        : "settings";
			return;
		}
		if (app.screen == Screen::server_lobby || app.screen == Screen::client_lobby)
		{
			app.return_home();
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
			const bool single_scenario = app.session->catalog().maps.size() == 1;
			app.menu_branch = single_scenario ? Branch::host : Branch::scenarios;
			focus_next = single_scenario ? "menu.new" : "menu.scenario/" + app.selected_scenario;
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
			if (file->purpose == FilePurpose::PlayerAvatar)
			{
				if (file->bytes.size() > 8 * 1024 * 1024)
					throw std::runtime_error("Choose an avatar image up to 8 MiB");
				const auto dot = file->filename.find_last_of('.');
				const auto extension = dot == std::string::npos ? std::string{} : file->filename.substr(dot);
				auto image =
				    LoadImageFromMemory(extension.c_str(), file->bytes.data(), static_cast<int>(file->bytes.size()));
				if (!image.data)
					throw std::runtime_error("The avatar image cannot be opened");
				const int side = std::min(image.width, image.height);
				ImageCrop(&image,
				          {static_cast<float>((image.width - side) / 2), static_cast<float>((image.height - side) / 2),
				           static_cast<float>(side), static_cast<float>(side)});
				ImageResize(&image, 48, 48);
				int size = 0;
				auto *png = ExportImageToMemory(image, ".png", &size);
				UnloadImage(image);
				if (!png)
					throw std::runtime_error("The avatar image cannot be saved");
				Bytes avatar(png, png + size);
				MemFree(png);
				verify_player_avatar(avatar);
				if (avatar_screen)
				{
					if (!app.session->set_local_player_profile(avatar_screen, app.local_player_names.at(avatar_screen),
					                                           avatar))
						throw std::runtime_error("This local player is no longer assigned to a screen");
					notice = "Local player profile updated.";
				}
				else
				{
					app.avatar = std::move(avatar);
					apply_profile(app);
				}
			}
			else if (file->purpose == FilePurpose::GamePack)
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
#ifndef __EMSCRIPTEN__
		if (!game_active(app) && app.display_consoles.empty() && app.inspector_display < 0)
		{
			app.host_display = GetCurrentMonitor();
			const auto current = GetMonitorPosition(app.host_display);
			for (const auto &display : app.native_displays)
			{
				const auto position = GetMonitorPosition(display.id);
				if (position.x == current.x && position.y == current.y)
				{
					app.host_display = display.id;
					break;
				}
			}
		}
#endif
		if (auto result = take_copy_result())
			notice = result->empty() ? "Join Code copied." : *result;
		if (owner.lock() != app.session)
		{
			owner = app.session;
			opened_runtime = false;
			app.launch_screen = Screen::viewscreen;
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
		document.GetElementById("lobby")->SetProperty("display", "none");
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
		{
			ambient.update(0, false);
			app.accessible_status.clear();
			return false;
		}
		app.screen_controls.clear();
		app.screen_inputs.clear();
		actions.clear();
		auto branch = app.menu_branch;
		const bool modal = app.packs_popup || app.screen == Screen::settings;
		ambient.update(GetFrameTime(), !app.reduced_motion && !modal && app.screen == Screen::home);
		const bool playing = game_active(app);
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
		auto profile_controls = [&]
		{
			return heading("Your profile") +
			       (app.avatar.empty()
			            ? ""
			            : "<div id=\"profile-avatar\" style=\"display:block;width:48px;height:48px;\"></div>") +
			       field(app, "player-name", "Player name", app.player_name) +
			       add("menu.avatar", "Upload avatar...",
			           [&]
			           {
				           avatar_screen = 0;
				           request_file(imports, FilePurpose::PlayerAvatar);
			           }) +
			       add("menu.profile", "Use this profile",
			           [&]
			           {
				           apply_profile(app);
			           });
		};
		using Branch = Application::MenuBranch;
		std::string markup = "<div id=\"menu-scutter\" aria-hidden=\"true\"></div>"
		                     "<h1 id=\"menu-title\">Against the Stars</h1>";
		if (!playing)
		{
			if (app.screen == Screen::home || modal)
			{
				markup += "<div id=\"menu-profile\" class=\"menu-crt\"><div class=\"crt-screen\">"
				          "<div id=\"menu-profile-avatar\" class=\"crt-avatar\">" +
				          (app.avatar.empty() ? std::string("?") : std::string{}) + "</div><p class=\"crt-name\">" +
				          escaped(app.player_name) + "</p></div></div>";
				if (!modal)
					app.screen_readouts.emplace_back("menu-profile", localise("Your profile") + ": " + app.player_name);
			}
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
				if (app.session && (app.session->lobby().active || app.session->running()))
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
						                                              new_game(app);
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
					if (branch == Branch::scenarios ||
					    (branch == Branch::ships && app.session->catalog().maps.size() != 1))
					{
						markup += "<div class=\"menu-column\">" + heading("Scenarios");
						if (app.session->catalog().maps.empty())
							markup += paragraph("Enable mod packs to choose a scenario.");
						for (const auto &[id, scenario] : app.session->catalog().maps)
							markup += add("menu.scenario/" + id, scenario.value("name", id),
							              [&, id]
							              {
								              choose_scenario(app, id);
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
					{
						markup += profile_controls();
						markup += field(app, "join-code", "Join Code", app.network_code);
					}
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
						              apply_profile(app);
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
				markup += "<div class=\"lobby-panel\"><div class=\"lobby-invitation\">" +
				          heading(lobby.scenario.empty() ? "Lobby" : lobby.scenario) +
				          readout("menu.join-code", "Join Code: " + lobby.code);
				if (app.can_host && !lobby.code.empty())
				{
					try
					{
						prepare_qr(app, lobby.code);
						markup += "<div class=\"qr-crt\"><div class=\"menu-crt qr-terminal\"><div class=\"crt-screen\">"
						          "<div id=\"menu-join-qr\"></div></div></div></div>";
					}
					catch (const std::exception &error)
					{
						notice = error.what();
					}
				}
				markup += add("menu.copy-code", "Copy Join Code",
				              [&]
				              {
					              copy_text(app.session->lobby().code);
				              });
				if (!modal)
					markup += profile_controls();
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
				markup += "</div><div class=\"lobby-roster\">";
				for (const auto &slot : lobby.slots)
					markup += readout(
					    "menu.slot/" + slot.id,
					    slot.id + ": " +
					        (slot.host.empty() ? slot.mandatory ? "AI / " + slot.ship : localise("Empty") : slot.ship));
				for (const auto &member : lobby.members)
				{
					const bool crew_readying = member.role == JoinRole::ship_server && member.console.empty() &&
					                           std::any_of(lobby.members.begin(), lobby.members.end(),
					                                       [&](const auto &m)
					                                       {
						                                       return m.role == JoinRole::player && m.approved &&
						                                              m.connected && m.slot == member.slot;
					                                       }) &&
					                           std::none_of(lobby.consoles.begin(), lobby.consoles.end(),
					                                        [&](const auto &seat)
					                                        {
						                                        return seat.member == member.id && seat.local_screen;
					                                        });
					const std::string state = !member.connected             ? "Disconnected"
					                          : !member.approved            ? "Awaiting approval"
					                          : !member.verified            ? "Verifying content"
					                          : member.role == JoinRole::gm ? "Observer"
					                          : crew_readying               ? "Viewscreen"
					                          : member.ready                ? "Ready"
					                                                        : "Not Ready";
					markup +=
					    (member.avatar.empty() ? ""
					                           : "<div id=\"avatar-" + escaped(member.id) +
					                                 "\" style=\"display:block;width:48px;height:48px;\"></div>") +
					    readout("menu.member/" + member.id,
					            member.name + " (" +
					                localise(member.role == JoinRole::ship_server ? "Ship host"
					                         : member.role == JoinRole::gm        ? "GM"
					                                                              : "Player") +
					                ") / " + member.slot + (member.console.empty() ? "" : " / " + member.console) +
					                " / " + localise(state));
				}
				markup += "<p id=\"lobby-countdown\"></p>";
				for (const auto &[id, text] : app.session->lobby_controls())
					if (!id.starts_with("lobby.seat/"))
						markup += add(id, text,
						              [&, id]
						              {
							              app.session->session_action(id);
						              });
#ifndef __EMSCRIPTEN__
				if (app.can_host)
					markup += paragraph("This screen is the selected ship's Viewscreen.");
#endif
				markup += heading("Consoles");
				const auto own = std::find_if(lobby.members.begin(), lobby.members.end(),
				                              [&](const auto &member)
				                              {
					                              return member.id == lobby.self;
				                              });
				for (const auto &seat : lobby.consoles)
				{
					const auto member = std::find_if(lobby.members.begin(), lobby.members.end(),
					                                 [&](const auto &m)
					                                 {
						                                 return m.id == seat.member;
					                                 });
					const auto claimant = member == lobby.members.end() ? localise("Unclaimed") : member->name;
					markup += heading(seat.name) +
					          readout("menu.console/" + seat.slot + "/" + seat.id, seat.slot + " / " + claimant +
					                                                                   (seat.member.empty() ? ""
					                                                                    : seat.ready ? " / Ready"
					                                                                                 : " / Not Ready"));
					if (own != lobby.members.end() && own->approved && own->role == JoinRole::player &&
					    (seat.member.empty() || seat.member == lobby.self))
						markup += add("lobby.seat/" + seat.slot + "/" + seat.id,
						              seat.member == lobby.self ? "Selected console" : "Choose this console",
						              [&, seat]
						              {
							              app.session->lobby_command(
							                  {LobbyAction::select_console, "", seat.slot, "", seat.id});
						              });
#ifndef __EMSCRIPTEN__
					if (app.can_host && lobby.coordinator && own != lobby.members.end() && seat.slot == own->slot &&
					    (seat.member.empty() || seat.local_screen))
					{
						if (seat.local_screen && member != lobby.members.end())
						{
							const auto route = seat.local_screen;
							app.local_player_names.try_emplace(route, member->name);
							markup +=
							    field(app, "local-name/" + std::to_string(route), "Local player name",
							          app.local_player_names.at(route)) +
							    add("menu.local-profile/" + std::to_string(route), "Use this name",
							        [&, route, avatar = member->avatar]
							        {
								        if (!app.session->set_local_player_profile(
								                route, app.local_player_names.at(route), avatar))
									        throw std::runtime_error("Local player is no longer assigned");
							        }) +
							    add("menu.local-avatar/" + std::to_string(route), "Upload avatar...",
							        [&, route]
							        {
								        avatar_screen = route;
								        request_file(imports, FilePurpose::PlayerAvatar);
							        }) +
							    add("menu.local-ready/" + std::to_string(route), seat.ready ? "Not Ready" : "Ready",
							        [&, route, ready = seat.ready]
							        {
								        LobbyCommand command{ready ? LobbyAction::unready : LobbyAction::ready};
								        command.screen = route;
								        app.session->lobby_command(command);
							        });
						}
						for (const auto &display : app.native_displays)
							if (display.id != app.host_display)
								markup += add("menu.console-screen/" + seat.id + "/" + std::to_string(display.id),
								              std::string(seat.local_screen == static_cast<unsigned>(display.id + 2)
								                              ? "On "
								                              : "Put on ") +
								                  display.name,
								              [&, seat, monitor = display.id]
								              {
									              LobbyCommand command{LobbyAction::assign_local_console, "", seat.slot,
									                                   "", seat.id};
									              command.screen = monitor + 2;
									              if (!app.session->lobby_command(command))
										              throw std::runtime_error("This console is already claimed");
									              std::erase_if(app.display_consoles,
									                            [&](const auto &entry)
									                            {
										                            return entry.second == seat.id;
									                            });
									              app.display_consoles[monitor] = seat.id;
									              app.local_player_names.erase(command.screen);
									              if (app.inspector_display == monitor)
										              app.inspector_display = -1;
									              if (app.apply_displays)
										              app.apply_displays();
								              });
						if (seat.local_screen)
							markup += add("menu.clear-console-screen/" + seat.id, "Remove from local screen",
							              [&, seat]
							              {
								              LobbyCommand command{LobbyAction::assign_local_console};
								              command.screen = seat.local_screen;
								              if (app.session->lobby_command(command))
									              app.display_consoles.erase(static_cast<int>(seat.local_screen) - 2);
								              if (app.apply_displays)
									              app.apply_displays();
							              });
					}
#endif
				}
				if (own != lobby.members.end() && own->role == JoinRole::gm)
					for (const auto &slot : lobby.slots)
						if (!slot.host.empty())
							markup += add("lobby.seat/" + slot.id + "/", "Inspect " + slot.id,
							              [&, slot]
							              {
								              app.session->lobby_command({LobbyAction::select_console, "", slot.id});
							              });
#ifndef __EMSCRIPTEN__
				if (app.can_host && lobby.coordinator)
				{
					markup += heading("GM inspector");
					for (const auto &display : app.native_displays)
						if (display.id != app.host_display)
							markup +=
							    add("menu.inspector-screen/" + std::to_string(display.id),
							        std::string(app.inspector_display == display.id ? "On " : "Put on ") + display.name,
							        [&, monitor = display.id]
							        {
								        LobbyCommand command{LobbyAction::assign_local_console};
								        command.screen = monitor + 2;
								        app.session->lobby_command(command);
								        app.display_consoles.erase(monitor);
								        app.inspector_display = monitor;
								        if (app.apply_displays)
									        app.apply_displays();
							        });
					if (app.inspector_display >= 0)
						markup += add("menu.clear-inspector-screen", "Remove from local screen",
						              [&]
						              {
							              app.inspector_display = -1;
						              });
				}
#endif
				markup += add("menu.leave", "Leave Game",
				              [&]
				              {
					              app.session->lobby_command({LobbyAction::leave});
					              app.return_home();
				              }) +
				          "</div></div>";
			}
			if (branch != Branch::root || app.screen == Screen::server_lobby || app.screen == Screen::client_lobby)
				markup += "<div class=\"menu-back\">" +
				          add("menu.back", "Back",
				              [&]
				              {
					              back(app);
				              }) +
				          "</div>";
		}
		if (modal)
		{
			button_enabled = true;
			markup += "<div class=\"modal-shade\"><div class=\"menu-dialog\">";
			if (app.screen == Screen::settings)
			{
				markup += profile_controls();
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
				markup += add("menu.dust", app.dust_motes ? "Dust motes: On" : "Dust motes: Off",
				              [&]
				              {
					              app.dust_motes = !app.dust_motes;
					              remember_preferences(app);
				              });
				markup += add("menu.grid", app.reference_grid ? "Reference grid: On" : "Reference grid: Off",
				              [&]
				              {
					              app.reference_grid = !app.reference_grid;
					              remember_preferences(app);
				              });
				if (playing)
				{
					for (const auto &[id, text] : app.session->lobby_controls())
						if (id.starts_with("lobby.approve/") || id.starts_with("network.approve/") ||
						    id.starts_with("network.gm/"))
							markup += add(id, text,
							              [&, id]
							              {
								              app.session->session_action(id);
							              });
					if (app.can_host && app.session->running())
					{
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
					markup += add("settings.quit", "Quit to Main Menu",
					              [&]
					              {
						              invoke_navigation(app, "settings.quit");
					              });
				}
				if (app.reduced_motion)
					markup += paragraph("Reduced motion hides dust motes and the reference grid.");
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
						              draft = app.library->toggle_pack(draft, draft[i].hash);
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
					    if (!app.session)
						    throw std::runtime_error("Choose packs from the main menu before opening Workshop.");
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
			if (id == "menu.back" && (!game_active(app) || app.screen == Screen::settings))
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
		const bool compact = region.width < 760;
		if (auto *scutter = document.GetElementById("menu-scutter"))
		{
			const auto progress = ambient.crossing_progress();
			scutter->SetProperty("display", progress < 0 ? "none" : "block");
			if (progress >= 0)
			{
				const float width = std::clamp(region.height * .22f, 96.f, 192.f);
				const float height = width / scutter_aspect;
				const float travel = static_cast<float>(progress) * (region.width + width);
				const float bob =
				    static_cast<float>(std::sin(ambient.scutter.age * (ambient.fast_crossing ? 16 : 6))) * 1.2f;
				scutter->SetProperty("width", std::to_string(width) + "px");
				scutter->SetProperty("height", std::to_string(height) + "px");
				scutter->SetProperty("left",
				                     std::to_string(ambient.rightward ? travel - width : region.width - travel) + "px");
				scutter->SetProperty("top", std::to_string(region.height * .99f - height + bob) + "px");
				scutter->SetProperty("transform", ambient.rightward ? "none" : "scaleX(-1)");
			}
		}
		if (auto *profile = document.GetElementById("menu-profile"))
		{
			Rml::ElementList screens;
			profile->GetElementsByClassName(screens, "crt-screen");
			for (auto *screen : screens)
				screen->SetProperty("opacity", std::to_string(ambient.television_opacity()));
		}
		const float profile_width = compact ? 140.8f : 264.f;
		if (auto *profile = document.GetElementById("menu-profile"))
		{
			profile->SetProperty("width", std::to_string(profile_width) + "px");
			profile->SetProperty("height", std::to_string(profile_width * terminal_aspect) + "px");
			profile->SetProperty("font-size", compact ? "11px" : "16px");
		}
		if (auto *avatar = document.GetElementById("menu-profile-avatar"))
		{
			avatar->SetProperty("width", compact ? "24px" : "48px");
			avatar->SetProperty("height", compact ? "24px" : "48px");
		}
		if (auto *title = document.GetElementById("menu-title"))
		{
			title->SetProperty("top", compact ? "188px" : "30px");
			title->SetProperty("font-size", compact ? "38px" : "56px");
		}
		const float reserved_right = branch != Branch::root && !compact ? profile_width + 24.f : 0.f;
		if (auto *viewport = document.GetElementById("menu-viewport"))
		{
			viewport->SetProperty("right", std::to_string(reserved_right) + "px");
			viewport->SetProperty("top", compact ? "246px" : "120px");
		}
		for (const auto &input : app.screen_inputs)
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document.GetElementById(input.id)))
				if (field->GetValue() != input.value)
					field->SetValue(input.value);
		const int columns = branch == Branch::ships       ? (app.session->catalog().maps.size() == 1 ? 3 : 4)
		                    : branch == Branch::scenarios ? 3
		                    : branch == Branch::root      ? 1
		                                                  : 2;
		const float column_width = std::min(320.f, std::max(std::min(250.f, region.width - reserved_right - 48.f),
		                                                    (region.width - reserved_right - 48.f) / columns));
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
		const auto status = !app.status.empty() ? app.status
		                    : !notice.empty()   ? notice
		                    : app.session       ? app.session->lobby().status
		                                        : std::string{};
		document.GetElementById("menu-status")->SetInnerRML(escaped(localise(status)));
		app.accessible_status = localise(status);
		if (!app.screen_readouts.empty())
			app.accessible_status += " " + app.screen_readouts.back().second;
		const auto lobby = app.session ? app.session->lobby() : LobbyProjection{};
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
			if (app.can_host && app.apply_displays)
				app.apply_displays();
			app.screen = app.can_host ? Screen::viewscreen
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
void NativeMenuScreen::draw_backdrop(const Application &app, Rml::ElementDocument &document)
{
	auto *root = document.GetElementById("native-menu");
	if (!app.native_menu || !root || !root->IsVisible() || !state_->panorama.id)
	{
		state_->flash.update(0, false);
		return;
	}
	const auto at = root->GetAbsoluteOffset(Rml::BoxArea::Content);
	const float width = root->GetClientWidth(), height = root->GetClientHeight();
	if (height <= 0 || width <= 0)
		return;
	if (!app.reduced_motion)
		state_->panorama_phase = std::fmod(state_->panorama_phase + GetFrameTime() / state_->panorama_period, 1.0);
	const auto texture = state_->panorama;
	const float tile = std::max(height * texture.width / texture.height, width / 2);
	const float offset = static_cast<float>(state_->panorama_phase * 2 * tile);
	const float flash = state_->flash.update(GetFrameTime(), !app.reduced_motion);
	BeginScissorMode(static_cast<int>(at.x), static_cast<int>(at.y), static_cast<int>(width), static_cast<int>(height));
	// Adjacent copies share the same edge texels. Mirroring keeps the loop continuous.
	for (int index = 0; index * tile - offset < width; ++index)
		DrawTexturePro(
		    texture,
		    {0, 0, static_cast<float>(index % 2 ? -texture.width : texture.width), static_cast<float>(texture.height)},
		    {at.x + index * tile - offset, at.y, tile, height}, {0, 0}, 0, WHITE);
	if (flash > 0 && state_->flash_overlay.id)
	{
		BeginBlendMode(BLEND_ADDITIVE);
		for (int index = 0; index * tile - offset < width; ++index)
			DrawTexturePro(state_->flash_overlay,
			               {0, 0, static_cast<float>(index % 2 ? -texture.width : texture.width),
			                static_cast<float>(texture.height)},
			               {at.x + index * tile - offset, at.y, tile, height}, {0, 0}, 0, Fade(WHITE, flash));
		EndBlendMode();
	}
	EndScissorMode();
}
void NativeMenuScreen::draw(const Application &app, Rml::ElementDocument &document)
{
	if (!app.native_menu || (app.screen != Screen::home && app.screen != Screen::server_lobby &&
	                         app.screen != Screen::client_lobby && app.screen != Screen::settings))
		return;
	auto draw = [&](const std::string &id, Texture2D texture, bool integer_scale = false)
	{
		if (!texture.id)
			return;
		if (auto *element = document.GetElementById(id); element && element->IsVisible())
		{
			const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Content);
			auto *root = document.GetElementById("native-menu");
			const auto origin = root->GetAbsoluteOffset(Rml::BoxArea::Content);
			Rectangle clip{origin.x, origin.y, root->GetClientWidth(), root->GetClientHeight()};
			for (auto *parent = element->GetParentNode(); parent; parent = parent->GetParentNode())
				if (parent->IsClassSet("lobby-invitation") || parent->IsClassSet("lobby-roster") ||
				    parent->IsClassSet("menu-column") || parent->IsClassSet("menu-dialog") ||
				    parent->IsClassSet("crt-screen") || parent->IsClassSet("qr-crt"))
				{
					const auto position = parent->GetAbsoluteOffset(Rml::BoxArea::Content);
					const float right = std::min(clip.x + clip.width, position.x + parent->GetClientWidth());
					const float bottom = std::min(clip.y + clip.height, position.y + parent->GetClientHeight());
					clip.x = std::max(clip.x, position.x);
					clip.y = std::max(clip.y, position.y);
					clip.width = std::max(0.f, right - clip.x);
					clip.height = std::max(0.f, bottom - clip.y);
				}
			if (clip.width <= 0 || clip.height <= 0)
				return;
			BeginScissorMode(static_cast<int>(clip.x), static_cast<int>(clip.y), static_cast<int>(clip.width),
			                 static_cast<int>(clip.height));
			float scale =
			    std::min(element->GetClientWidth() / texture.width, element->GetClientHeight() / texture.height);
			if (integer_scale)
				scale = std::floor(scale);
			if (scale > 0)
			{
				const float width = texture.width * scale, height = texture.height * scale;
				DrawTexturePro(texture, {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)},
				               {at.x + (element->GetClientWidth() - width) / 2,
				                at.y + (element->GetClientHeight() - height) / 2, width, height},
				               {0, 0}, 0,
				               id == "menu-profile-avatar" ? Fade(WHITE, state_->ambient.television_opacity()) : WHITE);
			}
			EndScissorMode();
		}
	};
	if (!app.avatar.empty())
	{
		draw("profile-avatar", state_->avatar_textures.load(sha256(app.avatar), app.avatar));
		if (app.screen != Screen::settings && !app.packs_popup)
			draw("menu-profile-avatar", state_->avatar_textures.load(sha256(app.avatar), app.avatar));
	}
	if (app.session)
		for (const auto &member : app.session->lobby().members)
			if (!member.avatar.empty())
				draw("avatar-" + member.id, state_->avatar_textures.load(sha256(member.avatar), member.avatar));
	if (app.can_host && app.screen == Screen::server_lobby && !app.packs_popup)
	{
		draw("menu-join-qr", state_->join_qr, true);
	}
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
	const auto scene = parse_json(read_file(asset_directory + "/menu/menu-scene.json", 64 * 1024), "menu scene");
	if (scene.at("schema") != 1 || scene.at("panorama").at("repeat") != "mirror-x")
		throw std::runtime_error("Unsupported menu scene");
	for (const auto *part : {"panorama", "foreground", "terminal", "flash", "scutter"})
	{
		const auto &asset = scene.at(part);
		const auto file = asset.at("file").get<std::string>();
		if (file.empty() || file.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.") != std::string::npos ||
		    !file.ends_with(".png"))
			throw std::runtime_error("Invalid menu scene filename");
		const auto image = LoadImage((asset_directory + "/menu/" + file).c_str());
		const bool valid =
		    image.data && image.width == asset.at("size").at(0) && image.height == asset.at("size").at(1);
		if (valid && std::string(part) == "panorama")
		{
			if (state_->panorama.id)
				UnloadTexture(state_->panorama);
			state_->panorama = LoadTextureFromImage(image);
			SetTextureFilter(state_->panorama, TEXTURE_FILTER_BILINEAR);
		}
		UnloadImage(image);
		if (!valid)
			throw std::runtime_error("Menu scene image dimensions disagree with metadata");
	}
	state_->flash = MenuFlash::parse(scene.at("flash"));
	state_->ambient = MenuAmbient::parse(scene.at("flicker"), scene.at("scutter"));
	const auto ambient_seed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	state_->ambient.flicker.random ^= static_cast<std::uint32_t>(ambient_seed);
	state_->ambient.scutter.random ^=
	    static_cast<std::uint32_t>(ambient_seed >> 32) ^ static_cast<std::uint32_t>(ambient_seed * 2654435761u);
	const auto &scutter_size = scene.at("scutter").at("size");
	state_->scutter_aspect = scutter_size.at(0).get<float>() / scutter_size.at(1).get<float>();
	const auto &panorama = scene.at("panorama"), &flash = scene.at("flash");
	if (panorama.at("size") != flash.at("size"))
		throw std::runtime_error("Menu flash mask must align with panorama");
	auto base_image = LoadImage((asset_directory + "/menu/" + panorama.at("file").get<std::string>()).c_str());
	auto mask_image = LoadImage((asset_directory + "/menu/" + flash.at("file").get<std::string>()).c_str());
	if (!base_image.data || !mask_image.data)
	{
		UnloadImage(base_image);
		UnloadImage(mask_image);
		throw std::runtime_error("Could not load menu flash imagery");
	}
	auto *pixels = LoadImageColors(base_image);
	auto *mask = LoadImageColors(mask_image);
	for (int i = 0; i < base_image.width * base_image.height; ++i)
		pixels[i].a = mask[i].a;
	Image overlay{pixels, base_image.width, base_image.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
	if (state_->flash_overlay.id)
		UnloadTexture(state_->flash_overlay);
	state_->flash_overlay = LoadTextureFromImage(overlay);
	SetTextureFilter(state_->flash_overlay, TEXTURE_FILTER_BILINEAR);
	UnloadImageColors(pixels);
	UnloadImageColors(mask);
	UnloadImage(base_image);
	UnloadImage(mask_image);
	state_->panorama_period = scene.at("panorama").at("period_seconds").get<double>();
	state_->panorama_phase = scene.at("panorama").at("initial_phase").get<double>();
	if (!std::isfinite(state_->panorama_period) || state_->panorama_period <= 0 ||
	    !std::isfinite(state_->panorama_phase) || state_->panorama_phase < 0 || state_->panorama_phase >= 1)
		throw std::runtime_error("Invalid menu panorama timing");
	const auto &terminal = scene.at("terminal"), &screen = terminal.at("screen");
	const float terminal_width = terminal.at("size").at(0), terminal_height = terminal.at("size").at(1);
	const float x = screen.at("x"), y = screen.at("y"), width = screen.at("width"), height = screen.at("height");
	if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > terminal_width || y + height > terminal_height)
		throw std::runtime_error("Invalid CRT screen aperture");
	state_->terminal_aspect = terminal_height / terminal_width;
	std::string styles = state_->button_skin.sprite_sheet("menu-button") + panel.sprite_sheet("menu-panel");
	styles += "#native-menu { background:transparent; decorator:image(menu/" +
	          scene.at("foreground").at("file").get<std::string>() + " cover); }\n";
	styles += ".menu-crt { decorator:image(menu/" + terminal.at("file").get<std::string>() + " contain); }\n";
	styles += "#menu-scutter { position:absolute; pointer-events:none; decorator:image(menu/" +
	          scene.at("scutter").at("file").get<std::string>() + " contain); }\n";
	styles += ".crt-screen { left:" + std::to_string(x / terminal_width * 100) +
	          "%; top:" + std::to_string(y / terminal_height * 100) +
	          "%; width:" + std::to_string(width / terminal_width * 100) +
	          "%; height:" + std::to_string(height / terminal_height * 100) + "%; }\n";
	const float qr_crop = terminal.at("invitation_crop_top").get<float>() / terminal_width * 440;
	if (!std::isfinite(qr_crop) || qr_crop < 0 || qr_crop >= y / terminal_width * 440)
		throw std::runtime_error("Invalid invitation CRT crop");
	styles += ".qr-terminal { top:-" + std::to_string(qr_crop) +
	          "px; height:" + std::to_string(440 * state_->terminal_aspect) + "px; }\n";
	styles += ".qr-crt { height:" + std::to_string(440 * state_->terminal_aspect - qr_crop) + "px; }\n";
	styles += "#native-menu button.menu-skinned { decorator: " + state_->button_skin.decorator("menu-button") + "; }\n";
	styles += "#native-menu .menu-dialog { decorator: " + panel.decorator("menu-panel") + "; }\n";
	const auto &skin = state_->button_skin;
	styles += "#native-menu button.menu-skinned { padding: " +
	          std::to_string(std::max(skin.top, skin.bottom) * skin.scale + 4) + "px " +
	          std::to_string(std::max(skin.left, skin.right) * skin.scale + 8) + "px; }\n";
	styles += R"(
    #native-menu { color:#fff0d4; clip:always; }
    #native-menu h1 { color:#fff0d4; font-family:UnifrakturCook; font-weight:bold; font-size:56px; letter-spacing:0; }
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
    #native-menu .lobby-panel { left:5%; width:90%; display:flex; flex-direction:row; flex-wrap:wrap; align-items:flex-start; }
    #native-menu .lobby-invitation { width:440px; max-width:100%; flex-shrink:0; margin-right:24px; max-height:100%; overflow:auto; }
    #native-menu .lobby-roster { min-width:300px; flex-basis:300px; flex-grow:1; max-height:100%; overflow:auto; }
    #native-menu .lobby-panel input { width:100%; box-sizing:border-box; height:40px; margin:6px 0 12px; }
    #native-menu .modal-shade { background:#080705dd; }
    #native-menu scrollbarhorizontal, #native-menu scrollbarvertical { background:#1b1915; }
    #native-menu scrollbarhorizontal sliderbar, #native-menu scrollbarvertical sliderbar { background:#c7b895; }
    #native-menu input { background:#211f1a; color:#fff0d4; border-color:#a99875; }
    #menu-volume slidertrack { background:#60513a; }
    #menu-volume sliderbar { background:#e1893c; border-color:#fff0d4; }
    .menu-crt { display:block; position:relative; flex-shrink:0; }
    #menu-profile { position:absolute; top:0; right:20px; pointer-events:none; }
    .crt-screen { position:absolute; display:flex; flex-direction:column; align-items:center; justify-content:center; overflow:hidden; color:#cbe4c5; }
    .crt-avatar { display:block; flex-shrink:0; text-align:center; background:#15231a; color:#cbe4c5; }
    #native-menu .crt-name { display:block; width:100%; text-align:center; font-size:1em; margin:4px 0 0; max-height:40px; overflow:hidden; }
    .qr-crt { display:block; position:relative; width:440px; overflow:hidden; clip:always; }
    .qr-terminal { position:absolute; left:0; width:440px; }
    #menu-join-qr { display:block; width:100%; height:100%; }
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
