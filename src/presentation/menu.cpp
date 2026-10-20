module;
#include <raylib.h>
#include <qrcodegen.hpp>
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <functional>
#include <tuple>
#include <map>
#include <set>
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
#include "menu_images.hpp"
#include "settings.hpp"
namespace phoenix
{
struct NativeMenuScreen::State
{
	FileImportInbox imports;
	std::vector<LibraryPack> draft;
	std::string previous, notice, focus_next, action_error;
	std::string lobby_ship;
	std::optional<std::uint64_t> save_after;
	bool export_save = false, advanced = false, opened_runtime = false;
	std::weak_ptr<SessionPort> owner;
	Application::MenuBranch shown_branch = Application::MenuBranch::root;
	float column_offset = 24;
	bool button_enabled = true;
	MenuSkin button_skin;
	std::map<std::string, std::function<void()>> actions;
	Bytes join_qr;
	Texture2D panorama{}, debris{}, debris_light{};
	double debris_phase = .12, logo_elapsed = 0;
	Texture2D flash_overlay{};
	MenuFlash flash;
	MenuAmbient ambient;
	float scutter_aspect = 1;
	double panorama_phase = 0, panorama_period = 720;
	float terminal_aspect = 1;
	float terminal_width = 264;
	std::map<std::string, Bytes> crew_portraits;
	std::string join_qr_url;
	std::set<std::string> image_elements;
	std::uint64_t avatar_screen = 0;
	std::string avatar_directory;
	std::vector<Bytes> avatars;
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
		retain_menu_images({});
		if (panorama.id)
			UnloadTexture(panorama);
		if (debris.id)
			UnloadTexture(debris);
		if (debris_light.id)
			UnloadTexture(debris_light);
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
		int length = 0;
		auto *png = ExportImageToMemory(image, ".png", &length);
		UnloadImage(image);
		if (!png)
			throw std::runtime_error("Cannot prepare invitation image");
		join_qr.assign(png, png + length);
		MemFree(png);
		join_qr_url = url;
	}
	void update_images(const Application &app, Rml::ElementDocument &document)
	{
		std::set<std::string> sources, elements;
		const auto *root = document.GetElementById("native-menu");
		const auto image = [&](const std::string &id, const Bytes &png, const char *fit)
		{
			auto *element = document.GetElementById(id);
			if (!element || png.empty())
				return;
			const auto source = register_menu_image(png);
			const auto decoration = "image(" + source + " " + fit + ")";
			if (element->GetAttribute<std::string>("data-menu-image", "") != decoration)
			{
				element->SetProperty("decorator", decoration);
				element->SetAttribute("data-menu-image", decoration);
			}
			sources.insert(source);
			elements.insert(id);
		};
		if (app.native_menu && root && root->IsVisible())
		{
			if (!app.avatar.empty())
			{
				const auto key = sha256(app.avatar);
				const auto found = crew_portraits.find(key);
				image("profile-avatar", app.avatar, "contain");
				if (app.screen != Screen::settings && !app.packs_popup)
					image("menu-profile-avatar", found == crew_portraits.end() ? app.avatar : found->second, "cover");
			}
			if (app.session)
				for (const auto &member : app.session->lobby().members)
					image("avatar-" + member.id, member.avatar, "contain");
			if ((app.screen == Screen::server_lobby || app.screen == Screen::client_lobby) && !app.packs_popup)
				image("menu-join-qr", join_qr, "contain");
		}
		for (const auto &id : image_elements)
			if (!elements.contains(id))
				if (auto *element = document.GetElementById(id))
				{
					element->RemoveProperty("decorator");
					element->RemoveAttribute("data-menu-image");
				}
		image_elements = std::move(elements);
		// All document references have now been replaced or removed.
		retain_menu_images(sources);
	}
	void choose_scenario(Application &app, std::string id)
	{
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
		    (id == "menu.join-gm" && branch == Branch::join_gm) || id == "menu.scenario/" + app.selected_scenario ||
		    (id.starts_with("settings.tab/") && id.substr(13) == app.settings_tab);
		return "<button id=\"" + Rml::StringUtilities::EncodeRml(id) + "\" class=\"menu-skinned" +
		       (selected ? " menu-selected" : "") + "\"" + (button_enabled ? "" : " disabled") + ">" +
		       (id == "menu.settings" ? "<span class=\"menu-settings-icon\"/>" : "") +
		       "<span class=\"menu-button-label\">" + Rml::StringUtilities::EncodeRml(localise(label)) +
		       "</span></button>";
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
		app.screen_inputs.push_back({id, label, id == "player-name" && value == "<username>" ? "" : value});
		return "<label for=\"" + id + "\">" + Rml::StringUtilities::EncodeRml(localise(label)) +
		       "</label><input id=\"" + id + "\" type=\"text\" " +
		       (id == "player-name" ? "placeholder=\"&lt;username&gt;\" " : "") + "maxlength=\"1024\"/>";
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
			lobby_ship.clear();
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
#include "lobby_board.hpp"
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
		if (app.avatar.empty() && !avatars.empty())
		{
			app.avatar = avatars.at(GetRandomValue(0, 11));
			if (app.library)
			{
				app.library->preferences.avatar = app.avatar;
				app.library->remember();
			}
		}
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
			return "<h2>" + Rml::StringUtilities::EncodeRml(localise(label)) + "</h2>";
		};
		auto paragraph = [](const std::string &label)
		{
			return "<p>" + Rml::StringUtilities::EncodeRml(localise(label)) + "</p>";
		};
		auto readout = [&](const std::string &id, const std::string &label, const std::string &accessible = "")
		{
			if (!modal)
				app.screen_readouts.emplace_back(id, accessible.empty() ? localise(label) : accessible);
			return "<p id=\"" + Rml::StringUtilities::EncodeRml(id) + "\">" +
			       Rml::StringUtilities::EncodeRml(localise(label)) + "</p>";
		};
		auto profile_controls = [&]
		{
			std::string choices = "<div class=\"avatar-choices\">";
			for (int i = 0; i < 12; ++i)
			{
				button(app, "menu.avatar-choice/" + std::to_string(i), "Avatar " + std::to_string(i + 1),
				       [&, i]
				       {
					       app.avatar = avatars.at(i);
					       apply_profile(app);
				       });
				choices += "<button class=\"avatar-choice\" id=\"menu.avatar-choice/" + std::to_string(i) +
				           "\"><img src=\"" + avatar_directory + "/avatar-" + std::to_string(i + 1) +
				           ".png\" width=\"48\" height=\"48\"/></button>";
			}
			choices += "</div>";
			return field(app, "player-name", "Player name", app.player_name) + choices +
			       add("menu.avatar", "Upload avatar...",
			           [&]
			           {
				           avatar_screen = 0;
				           request_file(imports, FilePurpose::PlayerAvatar);
			           }) +
			       "<div class=\"profile-actions\">" +
			       add("menu.profile", "Use this profile",
			           [&]
			           {
				           apply_profile(app);
				           back(app);
			           }) +
			       add("menu.close-dialog", "Cancel",
			           [&]
			           {
				           back(app);
			           }) +
			       "</div>";
		};
		using Branch = Application::MenuBranch;
		std::string markup = "<h1 id=\"menu-title\">Against the Stars</h1><div id=\"menu-frame\"></div>";
		if (!playing)
		{
			if (app.screen == Screen::home || modal)
			{
				button(app, "menu-profile", "Edit profile: " + app.player_name,
				       [&]
				       {
					       app.settings_return = app.screen;
					       app.screen = Screen::settings;
					       app.settings_tab = "profile";
					       focus_next = "player-name";
				       });
				markup += "<button id=\"menu-profile\" class=\"menu-crt\"><div class=\"crt-screen\">"
				          "<div id=\"menu-profile-avatar\" class=\"crt-avatar\">" +
				          (app.avatar.empty() ? std::string("?") : std::string{}) +
				          "</div><p id=\"menu-profile-name\" class=\"crt-name\"></p></div></button>";
			}
			if (app.screen == Screen::home || modal)
			{
				markup += "<div class=\"menu-top\">" +
				          add("menu.settings", "Settings",
				              [&]
				              {
					              app.settings_return = app.screen;
					              app.screen = Screen::settings;
					              focus_next = "settings.tab/" + app.settings_tab;
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
			}
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
						markup += add("menu.host-gm", "GM",
						              [&]
						              {
							              GameSetup setup{app.selected_scenario};
							              setup.gm_host = true;
							              app.session->prepare_lobby(setup);
							              app.screen = Screen::server_lobby;
							              focus_next = "lobby.ready";
						              });
						for (const auto &slot : app.session->catalog().maps.at(app.selected_scenario).at("ship_slots"))
						{
							const std::string slot_id = slot.at("id");
							markup += heading(slot_id);
							const auto default_ship =
							    slot.value("default_ship", slot.at("allowed_ships").front().get<std::string>());
							markup +=
							    add("menu.default/" + slot_id,
							        "Default: " +
							            app.session->catalog().entities.at(default_ship).value("name", default_ship),
							        [&, slot_id, default_ship]
							        {
								        app.session->prepare_lobby(
								            {app.selected_scenario, {{slot_id, default_ship}}, slot_id, ""});
								        app.screen = Screen::server_lobby;
								        focus_next = "lobby.ready";
							        });
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
				markup += lobby_board(app, add, heading, paragraph, readout);
			}
			if (app.screen == Screen::home && branch != Branch::root)
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
				const auto tabs = settings_ui::tabs(app, &focus_next);
				markup += heading("Settings") + "<div class=\"settings-tabs\" role=\"tablist\">";
				for (const auto &[id, label] : tabs)
					markup += add(id, label,
					              []
					              {
					              });
				markup += "</div><div class=\"settings-content\" role=\"tabpanel\" aria-labelledby=\"settings.tab/" +
				          app.settings_tab + "\">";
				if (app.settings_tab == "audio")
				{
					markup += settings_ui::audio_markup(true);
					settings_ui::register_audio(app, true);
				}
				else if (app.settings_tab == "profile")
				{
					markup += profile_controls();
				}
				else if (app.settings_tab == "language")
				{
					markup += add("menu.language",
					              "Language: " + (app.language == "en" ? std::string("English") : app.language),
					              [&]
					              {
						              const auto languages = installed_languages();
						              auto found = std::find(languages.begin(), languages.end(), app.language);
						              app.language = found == languages.end() || ++found == languages.end()
						                                 ? languages.front()
						                                 : *found;
						              remember_preferences(app);
					              });
					markup +=
					    add("menu.pseudo", app.pseudo_locale ? "Pseudo-localisation: On" : "Pseudo-localisation: Off",
					        [&]
					        {
						        app.pseudo_locale = !app.pseudo_locale;
						        remember_preferences(app);
					        });
				}
				else if (app.settings_tab == "display")
				{
					const auto controls = settings_ui::display_controls(app);
					for (const auto index : {2, 0, 1})
					{
						const auto &[action, text] = controls[index];
						markup += add("menu." + action.substr(9), text,
						              [&, action]
						              {
							              invoke_navigation(app, action);
						              });
					}
					if (app.reduced_motion)
						markup += paragraph("Reduced motion hides dust motes and the reference grid.");
				}
				else if (app.settings_tab == "session")
				{
					for (const auto &[id, text] : settings_ui::approvals(app))
						markup += add(id, text,
						              [&, id]
						              {
							              settings_ui::session_action(app, id);
						              });
					if (playing)
					{

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
						if (app.settings_return == Screen::viewscreen)
							markup += "<div id=\"native-sidebar-settings\"/>";
						markup += add("settings.quit", "Quit to Main Menu",
						              [&]
						              {
							              invoke_navigation(app, "settings.quit");
						              });
					}
				}
				markup += "</div>";
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
					markup += "<div class=\"pack-row\">" + paragraph(draft[i].id);
					const auto manifest = read_pack(app.library->read_pack_archive(draft[i].hash)).manifest();
					if (manifest.contains("parent"))
						markup += paragraph(localise("Parent mod") + ": " + manifest.at("parent").get<std::string>());
					if (draft[i].hash == app.library->base().hash)
					{
						markup += paragraph("Launch base (locked)") + "</div>";
						continue;
					}
					add("menu.pack-toggle/" + draft[i].hash, draft[i].enabled ? "Disable pack" : "Enable pack",
					    [&, i]
					    {
						    draft = app.library->toggle_pack(draft, draft[i].hash);
					    });
					markup += "<input type=\"checkbox\" id=\"menu.pack-toggle/" + draft[i].hash + "\"" +
					          (draft[i].enabled ? " checked" : "") + "/>";

					if (i > 1 || app.library->base().id.empty())
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
					markup += "</div>";
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
			if (app.screen != Screen::settings || app.settings_tab != "profile")
				markup += add("menu.close-dialog", "Back",
				              [&]
				              {
					              back(app);
				              });
			markup += "</div></div>";
		}
		markup += "<p id=\"menu-status\"></p>";
		if (!action_error.empty())
			markup += "<div id=\"menu-error\">" + heading("Unable to complete action") +
			          readout("menu-error-detail", action_error) +
			          add("menu.dismiss-error", "Dismiss",
			              [&]
			              {
				              action_error.clear();
			              }) +
			          "</div>";
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
				action_error.clear();
				callback();
				return true;
			}
			catch (const std::exception &error)
			{
				action_error = error.what();
				focus_next = "menu.dismiss-error";
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
		// Live name edits must not rebuild the form and discard its caret/selection.
		if (auto *name = document.GetElementById("menu-profile-name"))
		{
			const auto text = Rml::StringUtilities::EncodeRml(app.player_name);
			if (name->GetInnerRML() != text)
				name->SetInnerRML(text);
		}
		layout_lobby(app, document, region);
		const bool compact = region.width < 900;
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
		const float profile_width = terminal_width * (compact ? .533333f : 1.f);
		if (auto *profile = document.GetElementById("menu-profile"))
		{
			profile->SetProperty("width", std::to_string(profile_width) + "px");
			profile->SetProperty("height", std::to_string(profile_width * terminal_aspect) + "px");
			profile->SetProperty("font-size", compact ? "11px" : "16px");
		}
		if (auto *title = document.GetElementById("menu-title"))
		{
			const float logo_width = std::min(300.f, std::max(180.f, region.width - 740.f));
			const double target = (region.width - logo_width) / 2;
			const double t = std::clamp(logo_elapsed / 3., 0., 1.);
			const auto ease = [](double x)
			{
				return 1 - (1 - x) * (1 - x);
			};
			const double left = app.reduced_motion ? target
			                    : t < .8           ? region.width + (target - 18 - region.width) * ease(t / .8)
			                                       : target - 18 + 18 * ease((t - .8) / .2);
			title->SetProperty("top", "0px");
			title->SetProperty("left", std::to_string(left) + "px");
			title->SetProperty("width", std::to_string(logo_width) + "px");
			title->SetProperty("height", std::to_string(logo_width * 2 / 3) + "px");
		}
		const float reserved_right = 0.f;
		if (auto *viewport = document.GetElementById("menu-viewport"))
		{
			viewport->SetProperty("right", std::to_string(reserved_right) + "px");
			viewport->SetProperty("top", branch != Branch::root ? "200px" : "160px");
		}
		for (const auto &input : app.screen_inputs)
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document.GetElementById(input.id)))
				if (field->GetValue() != input.value)
					field->SetValue(input.value);
		const int columns = branch == Branch::ships       ? (app.session->catalog().maps.size() == 1 ? 3 : 4)
		                    : branch == Branch::scenarios ? 3
		                    : branch == Branch::root      ? 1
		                                                  : 2;
		const float column_width = std::min(320.f, (region.width - reserved_right - 48.f) / columns);
		const float target = 24.f;
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
			countdown->SetInnerRML(Rml::StringUtilities::EncodeRml(countdown_text));
			app.screen_readouts.emplace_back("lobby-countdown", countdown_text);
		}
		settings_ui::refresh_audio(app, document, true);
		const auto status = !action_error.empty() ? action_error
		                    : !app.status.empty() ? app.status
		                    : !notice.empty()     ? notice
		                    : app.session         ? app.session->lobby().status
		                                          : std::string{};
		document.GetElementById("menu-status")->SetInnerRML(Rml::StringUtilities::EncodeRml(localise(status)));
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
			app.screen = app.can_host ? (own != lobby.members.end() && own->role == JoinRole::gm ? Screen::gm
			                                                                                     : Screen::viewscreen)
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
	const bool prepared = state_->prepare(app, document);
	state_->update_images(app, document);
	return prepared;
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
	{
		state_->panorama_phase = std::fmod(state_->panorama_phase + GetFrameTime() / state_->panorama_period, 1.0);
		state_->debris_phase = std::fmod(state_->debris_phase + GetFrameTime() * 1.5 / state_->panorama_period, 1.0);
		state_->logo_elapsed += GetFrameTime();
	}
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
	if (state_->debris.id)
	{
		const auto debris = state_->debris;
		const float drift = static_cast<float>(state_->debris_phase * 2 * tile);
		for (int index = 0; index * tile - drift < width; ++index)
		{
			Rectangle source{0, 0, float(index % 2 ? -debris.width : debris.width), float(debris.height)};
			Rectangle destination{at.x + index * tile - drift, at.y, tile, height};
			if (flash > 0 && state_->debris_light.id)
			{
				BeginBlendMode(BLEND_ADDITIVE);
				for (const auto delta : {Vector2{-2, 0}, Vector2{2, 0}, Vector2{0, -2}, Vector2{0, 2}})
					DrawTexturePro(state_->debris_light, source,
					               {destination.x + delta.x, destination.y + delta.y, tile, height}, {0, 0}, 0,
					               Fade(WHITE, flash * .045f));
				EndBlendMode();
			}
			DrawTexturePro(debris, source, destination, {0, 0}, 0, WHITE);
		}
	}
	EndScissorMode();
}
void NativeMenuScreen::draw(const Application &app, Rml::ElementDocument &document)
{
	if (!app.native_menu || (app.screen != Screen::home && app.screen != Screen::server_lobby &&
	                         app.screen != Screen::client_lobby && app.screen != Screen::settings))
		return;
	if (app.packs_popup)
		for (const auto &pack : state_->draft)
		{
			for (const auto &action : {"toggle", "up", "down", "remove"})
			{
				auto *element = document.GetElementById("menu.pack-" + std::string(action) + "/" + pack.hash);
				if (!element || !element->IsVisible())
					continue;
				const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
				const float x = at.x + element->GetOffsetWidth() / 2, y = at.y + element->GetOffsetHeight() / 2;
				const Color colour{255, 240, 212, 255};
				if (std::string(action) == "toggle")
				{
					DrawRectangleLinesEx({x - 10, y - 10, 20, 20}, 2, colour);
					if (pack.enabled)
					{
						DrawLineEx({x - 6, y}, {x - 1, y + 5}, 2, colour);
						DrawLineEx({x - 1, y + 5}, {x + 7, y - 6}, 2, colour);
					}
				}
				else if (std::string(action) == "remove")
				{
					DrawRectangleLinesEx({x - 7, y - 5, 14, 15}, 2, colour);
					DrawLineEx({x - 10, y - 8}, {x + 10, y - 8}, 2, colour);
					DrawLineEx({x - 4, y - 11}, {x + 4, y - 11}, 2, colour);
				}
				else
				{
					const float d = std::string(action) == "up" ? -1.f : 1.f;
					DrawLineEx({x, y - 9 * d}, {x, y + 9 * d}, 2, colour);
					DrawLineEx({x - 7, y + 2 * d}, {x, y + 9 * d}, 2, colour);
					DrawLineEx({x + 7, y + 2 * d}, {x, y + 9 * d}, 2, colour);
				}
			}
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
	state_->avatar_directory = asset_directory + "/menu";
	for (int i = 1; i <= 12; ++i)
	{
		state_->avatars.push_back(read_file(state_->avatar_directory + "/avatar-" + std::to_string(i) + ".png", 12288));
		state_->crew_portraits.emplace(
		    sha256(state_->avatars.back()),
		    read_file(state_->avatar_directory + "/portrait-" + std::to_string(i) + ".png", 512 * 1024));
	}
	state_->button_skin = load("button-generic-9slice.sprite.json", .075f);
	if (!state_->button_skin.has_label)
		throw std::runtime_error("Menu button sidecar needs label_center");
	const auto panel = load("helm-background-9slice.sprite.json", .12f);
	const auto lobby_panel = load("lobby-panel-9slice.sprite.json", .12f);
	const auto scene = parse_json(read_file(asset_directory + "/menu/menu-scene.json", 64 * 1024), "menu scene");
	if (scene.at("schema") != 1 || scene.at("panorama").at("repeat") != "mirror-x")
		throw std::runtime_error("Unsupported menu scene");
	for (const auto *part : {"panorama", "foreground", "terminal", "flash", "scutter", "debris", "logotype"})
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
		if (valid && std::string(part) == "debris")
		{
			if (state_->debris.id)
				UnloadTexture(state_->debris);
			if (state_->debris_light.id)
				UnloadTexture(state_->debris_light);
			state_->debris = LoadTextureFromImage(image);
			auto *pixels = LoadImageColors(image);
			for (int i = 0; i < image.width * image.height; ++i)
				pixels[i] = Color{223, 205, 166, pixels[i].a};
			Image rim{pixels, image.width, image.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
			state_->debris_light = LoadTextureFromImage(rim);
			UnloadImageColors(pixels);
			SetTextureFilter(state_->debris, TEXTURE_FILTER_BILINEAR);
			SetTextureFilter(state_->debris_light, TEXTURE_FILTER_BILINEAR);
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
	state_->debris_phase = state_->panorama_phase;
	if (!std::isfinite(state_->panorama_period) || state_->panorama_period <= 0 ||
	    !std::isfinite(state_->panorama_phase) || state_->panorama_phase < 0 || state_->panorama_phase >= 1)
		throw std::runtime_error("Invalid menu panorama timing");
	const auto &terminal = scene.at("terminal"), &screen = terminal.at("screen");
	const float terminal_width = terminal.at("size").at(0), terminal_height = terminal.at("size").at(1);
	const float x = screen.at("x"), y = screen.at("y"), width = screen.at("width"), height = screen.at("height");
	if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > terminal_width || y + height > terminal_height)
		throw std::runtime_error("Invalid CRT screen aperture");
	state_->terminal_aspect = terminal_height / terminal_width;
	state_->terminal_width = terminal.value("display_width", 264.f);
	std::string styles = state_->button_skin.sprite_sheet("menu-button") + panel.sprite_sheet("menu-panel") +
	                     lobby_panel.sprite_sheet("lobby-card");
	styles += "#native-menu { background:transparent; }\n";
	styles += "#native-menu #menu-frame { position:absolute; left:0; top:0; width:100%; height:100%; "
	          "pointer-events:none; background:transparent; decorator:image(menu/" +
	          scene.at("foreground").at("file").get<std::string>() + " cover); }\n";
	styles +=
	    "#native-menu #menu-title { color:transparent; font-size:1px; pointer-events:none; decorator:image(menu/" +
	    scene.at("logotype").at("file").get<std::string>() + " contain); }\n";
	styles += "#native-menu .lobby-card { decorator:" + lobby_panel.decorator("lobby-card") + "; }\n";
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
    #native-menu button.menu-skinned { position:relative; min-height:54px; line-height:22px; background:transparent; border:2px transparent; color:#fff4df; text-align:center; image-color:#ffffff; }
    #native-menu button:hover, #native-menu button:focus { image-color:#ffce93; border-color:#ffad54; background:#17120c; }
    #native-menu button:active, #native-menu button.menu-selected { image-color:#ffb668; border-color:#f58330; background:#21170c; }
    #native-menu button:disabled { opacity:0.48; image-color:#b9b3a7; border-color:transparent; background:transparent; color:#ddd4c0; }
    .menu-button-label { display:block; position:relative; pointer-events:none; }
    #native-menu .menu-top { width:64px; }
    #native-menu .menu-top button.menu-skinned { decorator:none; padding:0; width:64px; height:64px; margin:0; }
    #native-menu .menu-top .menu-button-label { display:none; }
    .menu-settings-icon { position:absolute; left:4px; top:4px; width:54px; height:54px; decorator:image(menu/settings.png contain); pointer-events:none; }
    #native-menu .settings-tabs { display:flex; flex-wrap:wrap; margin-bottom:16px; }
    #native-menu .settings-tabs button { flex:1 0 110px; width:auto; min-height:48px; font-size:16px; padding:8px; margin:0 4px 4px 0; }
    #native-menu .settings-tabs .menu-selected { image-color:#ffd093; }
    #native-menu .menu-dialog { background:transparent; border:0px; padding:30px; top:5%; max-height:90%; }
    #native-menu .lobby-board { position:absolute; left:0; top:0; width:100%; height:100%; }
    #native-menu .lobby-heading { position:absolute; left:28px; top:22px; width:46%; }
    #native-menu .lobby-heading h2 { color:#fff0d4; font-family:UnifrakturCook; font-weight:bold; font-size:40px; line-height:40px; letter-spacing:0; margin:0 0 6px; }
    #native-menu .lobby-heading p { font-size:18px; margin:0; }
    #native-menu .lobby-code { position:absolute; top:20px; right:24px; display:block; text-align:center; }
    #native-menu .lobby-code #menu-join-qr { width:88px; height:88px; flex-shrink:0; margin-right:14px; }
    #native-menu .lobby-code button.lobby-qr { width:100%; min-height:0; padding:0; margin:0 0 8px; border:0px; background:#ffffff; flex-shrink:0; }
    #native-menu .lobby-code .lobby-qr #menu-join-qr { width:100%; height:100%; margin:0; pointer-events:none; }
    #native-menu .lobby-code p { margin:0 0 8px; font-size:19px; }
    #native-menu .lobby-card { position:absolute; box-sizing:border-box; padding:24px 18px 18px; background:#10191c; }
    #lobby-console-grid, #lobby-sidebar { position:absolute; }
    #lobby-sidebar { overflow:auto; clip:always; }
    #lobby-player { position:absolute; left:5%; top:24px; width:90%; bottom:120px; overflow:auto; }
    #native-menu .lobby-player-board .lobby-footer { height:90px; padding:8px; box-sizing:border-box; background:#10191cee; flex-direction:column; align-items:stretch; }
    #native-menu .lobby-player-board .lobby-footer p { font-size:14px; }
    #native-menu .lobby-player-board .lobby-footer div { justify-content:flex-end; }
    #native-menu .lobby-player-card { display:block; box-sizing:border-box; width:100%; padding:18px; margin:12px 0; background:#10191c; }
    #native-menu .lobby-footer { z-index:2; }
    #native-menu .lobby-footer button.lobby-action .menu-button-label { display:block; }
    #native-menu .lobby-card h2 { font-size:19px; margin:0 0 7px; }
    #native-menu .lobby-card p { font-size:14px; line-height:18px; margin:0 0 7px; }
    #native-menu .lobby-console-description { min-height:36px; }
    #native-menu button.lobby-action { position:relative; decorator:none; width:auto; min-height:30px; padding:4px 8px; margin:0 5px 5px 0; font-size:14px; line-height:18px; border:1px #746249; background:#181d1d; }
    #native-menu button.lobby-action .menu-button-label { position:relative; left:0; top:0; }
    #native-menu button.lobby-action.menu-selected { background:#42301b; border-color:#ffad54; }
    #native-menu .lobby-profile-choices { display:flex; flex-wrap:wrap; margin:1px 0 5px; }
    #native-menu .lobby-profile-choices button { flex:1 1 0px; padding:4px; font-size:13px; }
    #native-menu .lobby-display-choices { display:flex; align-items:center; margin:2px 0 5px; }
    #native-menu .lobby-display-choices p { width:48px; margin:0; flex-shrink:0; }
    #native-menu .lobby-display-choices div { display:flex; flex-wrap:wrap; }
    #native-menu .lobby-display-choices button { min-width:30px; padding:3px 5px; min-height:28px; margin:0 4px 0 0; }
    #native-menu .lobby-person { display:flex; align-items:center; }
    #native-menu .lobby-person p { flex-grow:1; }
    #native-menu .lobby-person button.lobby-action { min-height:24px; padding:2px 5px; font-size:12px; }
    #native-menu .lobby-seat-actions { display:flex; }
    #native-menu .lobby-compact { padding:20px 14px 12px; }
    #native-menu .lobby-compact h2 { font-size:17px; margin-bottom:4px; }
    #native-menu .lobby-compact p { font-size:13px; line-height:16px; margin-bottom:5px; }
    #native-menu .lobby-compact .lobby-console-description { min-height:32px; }
    #native-menu .lobby-compact button.lobby-action { min-height:26px; font-size:12px; line-height:16px; padding:3px 4px; margin-bottom:4px; }
    #native-menu .lobby-compact .lobby-profile-choices { margin-bottom:1px; }
    #native-menu .lobby-compact .lobby-display-choices p { width:40px; margin:0; }
    #native-menu .lobby-compact .lobby-display-choices button { min-width:27px; padding:2px 4px; min-height:26px; margin:0 3px 0 0; }
    #native-menu .lobby-footer { position:absolute; bottom:22px; left:26px; right:26px; height:45px; display:flex; align-items:center; justify-content:space-between; }
    #native-menu .lobby-footer .menu-settings-icon { display:none; }
    #native-menu #lobby-waiting .lobby-person { margin-bottom:2px; }
    #native-menu #lobby-waiting .lobby-person p { margin:0; font-size:13px; line-height:16px; }
    #native-menu #lobby-waiting .lobby-person button.lobby-action { min-height:20px; margin:0; padding:1px 4px; line-height:16px; }
    #native-menu .lobby-footer p { margin:0; font-size:16px; }
    #native-menu .lobby-footer div { display:flex; }
    #native-menu .lobby-footer button { min-width:108px; min-height:38px; font-size:16px; }
    #native-menu .avatar-choices { display:flex; flex-wrap:wrap; width:420px; margin:8px auto; }
    #native-menu .profile-actions { display:flex; }
    #native-menu .profile-actions button { width:50%; }
    #native-menu .settings-content label { display:block; }
    #native-menu .settings-content input[type=text] { display:block; box-sizing:border-box; width:100%; height:40px; margin:6px 0; }
    #native-menu .avatar-choice { width:64px; height:64px; padding:6px; margin:3px; }
    #native-menu .avatar-choice img { pointer-events:none; }
    #native-menu .pack-row { display:flex; align-items:center; margin:6px 0; }
    #native-menu .pack-row .menu-button-label { display:none; }
    #native-menu .pack-row input { width:24px; height:24px; margin:4px; }
    #native-menu .pack-row p { flex-grow:1; width:45%; }
    #native-menu .pack-row button.menu-skinned { decorator:none; width:44px; min-height:44px; padding:4px; margin:0 4px; }
    #native-menu .modal-shade { z-index:10; background:#080705dd; }
    #native-menu scrollbarhorizontal, #native-menu scrollbarvertical { background:#1b1915; }
    #native-menu scrollbarhorizontal sliderbar, #native-menu scrollbarvertical sliderbar { background:#c7b895; }
    #native-menu input { background:#211f1a; color:#fff0d4; border-color:#a99875; }
    #native-menu .audio-volume slidertrack { background:#60513a; }
    #native-menu .audio-volume sliderbar { background:#e1893c; border-color:#fff0d4; }
    .menu-crt { display:block; position:relative; flex-shrink:0; }
    #native-menu #menu-profile { position:absolute; top:0; right:20px; z-index:3; border:0px; background:transparent; padding:0px; }
    #menu-profile .crt-screen, #menu-profile .crt-avatar, #menu-profile .crt-name { pointer-events:none; }
    .crt-screen { position:absolute; display:flex; flex-direction:column; align-items:center; justify-content:center; overflow:hidden; color:#cbe4c5; }
    .crt-avatar { display:block; width:100%; height:85%; flex-shrink:0; text-align:center; background:#15231a; color:#cbe4c5; }
    #native-menu .crt-name { display:block; width:100%; height:15%; text-align:center; font-size:1em; margin:0; overflow:hidden; background:#101918; }
    .qr-crt { display:block; position:relative; width:440px; overflow:hidden; clip:always; }
    .qr-terminal { position:absolute; left:0; width:440px; }
    #menu-join-qr { display:block; width:100%; height:85%; }
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
		if (button->IsClassSet("lobby-action") || button->GetId() == "menu.settings" ||
		    button->GetId().starts_with("menu.pack-"))
			continue;
		const float width = button->GetOffsetWidth(), height = button->GetOffsetHeight();
		const auto anchor = state_->button_skin.label_position(width, height);
		label->SetProperty("left", std::to_string(anchor[0] - width * .5f) + "px");
		label->SetProperty("top", std::to_string(anchor[1] - height * .5f) + "px");
	}
}
} // namespace phoenix
