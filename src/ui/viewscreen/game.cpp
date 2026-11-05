module;
#include <stdexcept>
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#endif
#include <rlgl.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <qrcodegen.hpp>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.ui.viewscreen;
import phoenix.content.assets;
import phoenix.content.catalog;
import phoenix.content.controllers;
import phoenix.content.image_codec;
import phoenix.content.scenarios;

import phoenix.application;
import phoenix.console;
import phoenix.files;
import phoenix.session.api;
import phoenix.graphics.geometry;
import phoenix.graphics.assets;
import phoenix.graphics.radar;
import phoenix.application.preferences;
import phoenix.knowledge.read_models;

namespace phoenix
{
struct GameScreen::State
{
	FileImportInbox imports;
	std::string mission, slot, ship, seat, notice;
	ConsoleModel console_model;
	TextureCache textures;
	std::map<std::string, std::unique_ptr<RadarSweep>> sweeps;
	SceneRenderer scene;
	SceneFrame frame;
	SceneMotion motion;
	Camera3D frame_camera{};
	Json frame_lighting;
	double frame_time = 0;
	Texture2D invitation{};
	Texture2D viewscreen_frame{};
	std::string invitation_url;
	bool show_invitation = false;
	bool environment_ready = false;
	std::optional<BodyView> previous_handling_pose;
	std::uint64_t handling_tick = 0;
	Vector2 acceleration{}, camera_offset{};
	float plume_level = 0, ignition = 0;
	bool burning = false;
	SceneEnvironment environment;
	Json presentation_identity;
	std::string grid_definition;
	float camera_aspect = 1;
	float game_top = 200;
	std::optional<Rectangle> display_region;
	std::map<std::string, double> scene_metrics;
	std::optional<std::uint64_t> save_after;
	std::weak_ptr<SessionPort> owner;
	std::uint64_t generation = 0;
	std::vector<std::pair<std::string, std::string>> content_identity;
	std::shared_ptr<const Observation> observation;
	bool was_running = false;
	std::string previous_markup, previous_description, previous_network;
	bool markup_ready = false;
	bool preload_planned = false, preload_finished = false;
	std::vector<std::string> preload_images, preload_definitions, preload_ui;
	std::size_t preload_done = 0;
	std::string preload_error;
	std::string resource_mission;
	SurfaceLoad scene_loading;
	struct ConsolePreparation
	{
		SurfaceLoad loading;
		TextureCache textures;
		std::string identity;
		std::vector<std::string> images, files, ui_images;
		std::size_t done = 0;
	};
	std::map<ConsoleId, ConsolePreparation> console_preparation;
	~State()
	{
		close_resources();
	}
	void reset_session(bool resources_changed = false)
	{
		imports.reset();
		if (resources_changed)
			close_resources();
		mission.clear();
		slot.clear();
		ship.clear();
		notice.clear();
		seat.clear();
		console_model.reset();
		save_after.reset();
		observation.reset();
		was_running = false;
		environment_ready = false;
		previous_handling_pose.reset();
		environment = {};
		frame = {};
		motion.reset();
		presentation_identity = nullptr;
		grid_definition.clear();
		markup_ready = false;
		if (resources_changed)
		{
			preload_planned = preload_finished = false;
			preload_ui.clear();
			preload_images.clear();
			preload_definitions.clear();
			preload_done = 0;
			preload_error.clear();
		}
	}
	void bind_session(const Application &app)
	{
		const decltype(content_identity) empty;
		const auto &packs = app.session ? app.session->catalog().ordered_packs : empty;
		const auto current_generation = app.session ? app.session->generation() : 0;
		auto next_mission = app.session ? app.session->loading_scenario() : std::string{};
		// A worker can briefly publish no scenario during handover. Absence is not
		// a new asset requirement, and must not evict the ready scene.
		if (next_mission.empty() && content_identity == packs)
			next_mission = resource_mission;
		const bool resources_changed = content_identity != packs || resource_mission != next_mission;
		if (owner.lock() != app.session || generation != current_generation || resources_changed)
		{
			reset_session(resources_changed);
			owner = app.session;
			generation = current_generation;
			content_identity = packs;
			resource_mission = next_mission;
		}
	}
	std::string label(const Application &app, const std::string &text)
	{
		return translate(text, app.pseudo_locale);
	}
	std::string query(const std::string &value)
	{
		const char *hex = "0123456789ABCDEF";
		std::string result;
		for (unsigned char c : value)
		{
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
			    c == '.' || c == '~')
				result += c;
			else
			{
				result += '%';
				result += hex[c >> 4];
				result += hex[c & 15];
			}
		}
		return result;
	}
	void make_invitation(const Application &app, bool gm)
	{
		const auto &page = app.join_page;
		const bool https = page.starts_with("https://"), http = page.starts_with("http://");
		if ((!https && !http) || page.size() > 1024 || page.find_first_of("?#@\r\n\t ") != std::string::npos ||
		    page.find('\0') != std::string::npos)
			throw std::runtime_error("Enter a plain HTTP/HTTPS launcher URL without a query or fragment");
		const std::size_t begin = https ? 8u : 7u;
		const auto end = page.find('/', begin);
		if (page.substr(begin, end == std::string::npos ? std::string::npos : end - begin).empty())
			throw std::runtime_error("The launcher URL needs a host");
		if (!app.session || app.session->invitation_code().empty())
			throw std::runtime_error("No ship-server invitation is available");
		const auto url = page + "?type=" + (gm ? "workshop&mode=gm" : "client") +
		                 "&code=" + query(app.session->invitation_code()) + "&service=" + query(app.network_service) +
		                 "&base=" + query(app.base_reference);
		const auto qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
		auto image = GenImageColor((qr.getSize() + 8) * 4, (qr.getSize() + 8) * 4, WHITE);
		for (int y = 0; y < qr.getSize(); ++y)
			for (int x = 0; x < qr.getSize(); ++x)
				if (qr.getModule(x, y))
					ImageDrawRectangle(&image, (x + 4) * 4, (y + 4) * 4, 4, 4, BLACK);
		auto texture = LoadTextureFromImage(image);
		UnloadImage(image);
		if (!texture.id)
			throw std::runtime_error("Could not render the invitation QR");
		if (invitation.id)
			UnloadTexture(invitation);
		invitation = texture;
		invitation_url = url;
		show_invitation = true;
	}
	void next(std::string &value, const std::vector<std::string> &choices)
	{
		auto found = std::find(choices.begin(), choices.end(), value);
		value = choices.empty() ? "" : (found == choices.end() || ++found == choices.end() ? choices.front() : *found);
	}
	std::vector<std::string> missions(const Catalog &catalog)
	{
		std::vector<std::string> ids;
		for (const auto &[id, data] : catalog.maps)
			ids.push_back(id);
		return ids;
	}
	std::vector<std::string> slots(const Catalog &catalog)
	{
		std::vector<std::string> ids;
		if (catalog.maps.contains(mission))
			for (const auto &value : catalog.maps.at(mission).at("ship_slots"))
				ids.push_back(value.at("id"));
		return ids;
	}
	std::vector<std::string> ships(const Catalog &catalog)
	{
		if (catalog.maps.contains(mission))
			for (const auto &value : catalog.maps.at(mission).at("ship_slots"))
				if (value.at("id") == slot)
					return value.at("allowed_ships").get<std::vector<std::string>>();
		return {};
	}
	void normalise(const Catalog &catalog)
	{
		auto maps = missions(catalog);
		if (std::find(maps.begin(), maps.end(), mission) == maps.end())
			mission = maps.empty() ? "" : maps.front();
		auto available_slots = slots(catalog);
		if (std::find(available_slots.begin(), available_slots.end(), slot) == available_slots.end())
			slot = available_slots.empty() ? "" : available_slots.front();
		auto available_ships = ships(catalog);
		if (std::find(available_ships.begin(), available_ships.end(), ship) == available_ships.end())
			ship = available_ships.empty() ? "" : available_ships.front();
		auto available_seats = seats(catalog);
		if (std::find(available_seats.begin(), available_seats.end(), seat) == available_seats.end())
			seat = available_seats.empty() ? "" : available_seats.front();
	}
	std::vector<std::string> seats(const Catalog &catalog)
	{
		std::vector<std::string> result;
		if (catalog.entities.contains(ship))
			for (const auto &spec : resolve_consoles(catalog, ship))
				result.push_back(spec.id);
		return result;
	}
	bool action(Application &app, const std::string &id)
	{
		if (!feature_action_enabled(id))
			return false;
		bind_session(app);
		if (!app.session)
			return false;
		if (id.rfind("network.", 0) == 0 || id.rfind("lobby.", 0) == 0)
			return app.session->session_action(id);
		if (id.rfind("game.", 0) != 0)
			return false;
		try
		{
			auto &session = *app.session;
			if (id == "game.import" && !session.running())
				request_file(imports, FilePurpose::GamePack);
			else if (id == "game.hide-qr")
				show_invitation = false;
			else if (id == "game.copy-invite" && !invitation_url.empty())
			{
				copy_text(invitation_url);
				notice = "Copying invitation link...";
			}
			else if (id == "game.load")
				request_file(imports, FilePurpose::GameSave);
			else if (id == "game.save" && session.running())
			{
				save_after = session.ticks();
				notice = "Saving after the next local tick boundary.";
			}
			else if (id == "game.resume" && session.recovery_pending())
			{
				session.resume_local();
				notice = "Resuming at the saved tick. Waiting for the local console claim.";
				app.screen = Screen::console;
			}
			else if (id == "game.clear")
			{
				session.clear_packs();
				bind_session(app);
				notice = "Session ended; content selection cleared.";
			}
			else if (id == "game.mission" && !session.running())
			{
				next(mission, missions(session.catalog()));
				slot.clear();
				ship.clear();
			}
			else if (id == "game.slot" && !session.running())
			{
				next(slot, slots(session.catalog()));
				ship.clear();
			}
			else if (id == "game.ship" && !session.running())
				next(ship, ships(session.catalog()));
			else if (id == "game.seat" && !session.running())
				next(seat, seats(session.catalog()));
			else if (id == "game.fleet" && !session.running())
			{
				auto bundle = session.fleet_setup({mission, {{slot, ship}}, slot, seat});
				if (bundle.empty())
					return false;
				export_file("phoenix-fleet-setup.zip", bundle);
				notice = "Private fleet setup exported. Load it on each ship server and select a different member.";
			}
			else if (id == "game.start" && !session.running())
			{
				session.start({mission, {{slot, ship}}, slot, seat});
				bind_session(app);
				notice = "Session started.";
				app.screen = Screen::viewscreen;
			}
			else if (session.running())
			{
				console_model.refresh(app.session);
				const bool accepted = console_model.act(id);
				notice = console_model.announcement();
				return accepted;
			}
			else
				return false;
			normalise(session.catalog());
			return true;
		}
		catch (const std::exception &error)
		{
			notice = error.what();
			return false;
		}
	}
	Rectangle area()
	{
		if (display_region)
			return *display_region;
		if (game_top == 0)
			return {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
		return {20, game_top, static_cast<float>(GetScreenWidth() - 40),
		        std::max(80.f, static_cast<float>(GetScreenHeight()) - game_top - 20)};
	}
	Rectangle widget_rect(const Json &widget)
	{
		auto a = area();
		return {a.x + a.width * widget.at("x").get<float>() / 1000, a.y + a.height * widget.at("y").get<float>() / 1000,
		        a.width * widget.at("width").get<float>() / 1000, a.height * widget.at("height").get<float>() / 1000};
	}
	const Json *console_definition(const Application &app)
	{
		return console_model.layout();
	}
	std::string telemetry_description(const Application &app, const std::optional<BodyView> &body) const
	{
		auto description = "Tick " + std::to_string(app.session->ticks());
		if (body)
			description += " / position " + std::to_string(body->x / 1000) + ", " + std::to_string(body->y / 1000) +
			               " m / speed " +
			               std::to_string(static_cast<int>(
			                   std::hypot(static_cast<double>(body->vx), static_cast<double>(body->vy)) / 1000)) +
			               " m/s / heading " + std::to_string(body->heading / 1000) + " degrees.";
		return description;
	}
	void refresh_status(Application &app) const
	{
		if (!app.session || !app.session->running() ||
		    (app.screen != Screen::viewscreen && app.screen != Screen::console))
			return;
		auto description = telemetry_description(app, app.session->focus_body());
		if (app.session->networked())
			description += " " + app.session->connection_status();
		if (!notice.empty())
			description += " " + notice;
		if (!app.status.empty())
			description += " " + app.status;
		app.accessible_status = std::move(description);
	}
	void prepare(Application &app, Rml::ElementDocument &document)
	{
		bind_session(app);
		if (was_running && app.session && !app.session->running())
			reset_session();
		if (auto result = take_copy_result())
			notice = result->empty() ? "Invitation link copied." : *result;
		app.screen_controls.clear();
		app.screen_action = [this, &app, &document](const std::string &id)
		{
			if (!feature_action_enabled(id))
				return false;
			auto input = [&](const char *name)
			{
				auto *control = dynamic_cast<Rml::ElementFormControl *>(document.GetElementById(name));
				if (!control)
					throw std::runtime_error("Connection field is unavailable");
				return control->GetValue();
			};
			try
			{
				if ((id == "game.connect-client" || id == "game.connect-gm") && app.connect)
				{
					app.connect(input("join-code"), input("join-service"), input("join-profile"),
					            id == "game.connect-gm" ? JoinRole::gm : JoinRole::player);
					bind_session(app);
					app.status.clear();
					return true;
				}
				if (id == "game.qr-client" || id == "game.qr-gm")
				{
					app.join_page = input("join-page");
					make_invitation(app, id == "game.qr-gm");
					notice = "Scan or copy the invitation. The joining device must be able to reach the launcher and "
					         "service.";
					return true;
				}
			}
			catch (const std::exception &error)
			{
				notice = error.what();
				return false;
			}
			return action(app, id);
		};
		std::string markup, network_markup, description = "Choose a mode to begin.";
		game_top = 200;
		if (app.screen == Screen::client_lobby)
			description = "Connect to your ship server, verify its content and claim an available station.";
		if (app.screen == Screen::viewscreen || app.screen == Screen::console)
			description = "No local mission is running.";
		auto button = [&](const std::string &id, const std::string &text)
		{
			if (!feature_action_enabled(id))
				return std::string{};
			app.screen_controls.emplace_back(id, text);
			return "<button id=\"" + Rml::StringUtilities::EncodeRml(id) + "\">" +
			       Rml::StringUtilities::EncodeRml(label(app, text)) + "</button>";
		};
		if (app.session)
		{
			// The Workshop owns its file responses while active. Game imports have an explicit purpose.
			if (app.screen != Screen::workshop && app.screen != Screen::gm)
				if (auto file = imports.take(); file && file->status != ImportStatus::Cancelled)
				{
					try
					{
						if (!file->error.empty())
							throw std::runtime_error(file->error);
						if (file->purpose == FilePurpose::GamePack)
						{
							app.session->add_pack(file->bytes);
							bind_session(app);
							normalise(app.session->catalog());
							notice = "Verified pack added: " + file->filename;
						}
						if (file->purpose == FilePurpose::GameSave)
						{
							app.session->restore(file->bytes);
							bind_session(app);
							app.screen = Screen::server_lobby;
							notice =
							    "Verified runtime save loaded. Simulation paused; reclaim the local console to resume.";
						}
					}
					catch (const std::exception &error)
					{
						notice = error.what();
					}
				}
			if (save_after && app.session->running() && app.session->ticks() > *save_after)
			{
				try
				{
					if (!app.session->session_action("session.export-save"))
					{
						export_file("phoenix-session.zip", app.session->save());
						notice = "Runtime save exported with its exact selected packs.";
					}
				}
				catch (const std::exception &error)
				{
					notice = error.what();
				}
				save_after.reset();
			}
			const auto &catalog = app.session->catalog();
			normalise(catalog);
			const auto previous_announcement = console_model.announcement();
			console_model.refresh(app.session);
			if (notice == previous_announcement && console_model.announcement().empty())
				notice.clear();
			else if (console_model.announcement() != previous_announcement && !console_model.announcement().empty())
				notice = console_model.announcement();
			observation = console_model.observation();
			const auto focus_body =
			    observation ? std::optional(observation->own)
			                : (app.can_host && app.session->networked() ? app.session->focus_body() : std::nullopt);
			was_running = app.session->running();
			if (app.screen == Screen::client_lobby && observation)
				app.screen = Screen::console;
			if ((app.screen == Screen::home || app.screen == Screen::server_lobby) && app.can_host)
			{
				description = "Import base packs before dependent mods. ";
				for (const auto &[id, hash] : catalog.ordered_packs)
					description += id + " [" + hash.substr(0, 12) + "] ";
				if (app.session->recovery_pending())
				{
					description =
					    "Recovery lobby / tick " + std::to_string(app.session->ticks()) + ". No live station claim. ";
					markup = button("game.resume", "Reclaim local console and resume") +
					         button("game.load", "Load runtime save") +
					         button("game.clear", "Discard recovery and clear packs");
				}
				else if (!app.session->running())
				{
					markup = button("game.import", "Add ZIP pack") + button("game.clear", "Clear packs") +
					         button("game.load", "Load runtime save");
					if (!catalog.maps.empty())
						markup += button("game.mission", "Mission: " + mission) +
						          button("game.slot", "Ship slot: " + slot) + button("game.ship", "Ship: " + ship);
					if (!ship.empty())
						markup += button("game.seat", "Console: " + seat) + button("game.start", "Start session");
					if (!ship.empty() && app.session->networked())
						markup += button("game.fleet", "Export fleet setup");
				}
			}
			else if ((app.screen == Screen::viewscreen || app.screen == Screen::console) && app.session->running())
			{
				description = telemetry_description(app, focus_body);
				if (app.screen == Screen::console)
					if (const auto *console = console_definition(app))
					{
						for (const auto &widget : console->at("widgets"))
						{
							const auto percent = [&](const char *name)
							{
								return std::to_string(widget.at(name).get<double>() / 10) + "%";
							};
							markup += "<div class=\"widget\" style=\"left:" + percent("x") + ";top:" + percent("y") +
							          ";width:" + percent("width") + ";height:" + percent("height") + "\"><h3>" +
							          Rml::StringUtilities::EncodeRml(label(app, widget.at("label"))) + "</h3>";
							const std::string kind = widget.at("kind");
							const auto suffix = "/" + widget.at("id").get<std::string>();
							if (kind == "radar")
							{
								markup += button("game.contacts" + suffix, "Read current radar contacts");
								app.screen_controls.back().second += ". " + console_model.contacts_text();
							}
							if (kind == "throttle")
								markup += button("game.slower" + suffix, "Decrease desired speed") +
								          button("game.faster" + suffix, "Increase desired speed");
							if (kind == "heading")
								markup += button("game.left" + suffix, "Turn heading +5 degrees") +
								          button("game.right" + suffix, "Turn heading -5 degrees");
							if (kind == "status")
								markup += "<p>" + Rml::StringUtilities::EncodeRml(notice) + "</p>";
							markup += "</div>";
						}
					}
				if (app.screen == Screen::console)
				{
					const auto controls = console_model.gameplay_controls();
					const auto readouts = console_model.gameplay_readouts();
					std::erase_if(app.screen_readouts,
					              [](const auto &readout)
					              {
						              return readout.first.starts_with("game.readout/");
					              });
					if (!controls.empty() || !readouts.empty())
					{
						network_markup +=
						    "<div class=\"gameplay-panels\" style=\"max-height:220px;overflow:auto;padding:8px;\">";
						for (const auto &[id, text] : controls)
							network_markup += button(id, text);
						for (const auto &[id, text] : readouts)
						{
							app.screen_readouts.emplace_back(id, text);
							network_markup += "<p id=\"" + Rml::StringUtilities::EncodeRml(id) + "\">" +
							                  Rml::StringUtilities::EncodeRml(text) + "</p>";
						}
						network_markup += "</div>";
					}
				}
			}
			if (app.session->networked())
			{
				description += " " + app.session->connection_status();
				if (app.screen != Screen::gm && app.screen != Screen::workshop && app.screen != Screen::viewscreen)
					for (const auto &[id, text] : app.session->lobby_controls())
						network_markup += button(id, text);
			}
		}
		if (app.screen == Screen::client_lobby && app.connect && !observation)
		{
			markup = "<div class=\"connection\"><label>Network code (room/server)</label><input id=\"join-code\" "
			         "type=\"text\" value=\"" +
			         Rml::StringUtilities::EncodeRml(app.network_code) +
			         "\"/><label>Network service</label><input id=\"join-service\" type=\"text\" value=\"" +
			         Rml::StringUtilities::EncodeRml(app.network_service) +
			         "\"/><label>Local profile</label><input id=\"join-profile\" type=\"text\" value=\"" +
			         Rml::StringUtilities::EncodeRml(app.network_profile) + "\"/>" +
			         button("game.connect-client", "Connect console") +
			         button("game.connect-gm", "Request GM inspection") + "</div>";
		}
		if ((app.screen == Screen::home || app.screen == Screen::server_lobby) && app.can_host && app.session &&
		    app.session->networked())
		{
			if (show_invitation)
			{
				markup =
				    button("game.hide-qr", "Close invitation") + button("game.copy-invite", "Copy invitation link");
				description = invitation_url;
			}
			else
				markup += "<div class=\"connection\"><label>Launcher page URL for joining devices</label><input "
				          "id=\"join-page\" type=\"text\" value=\"" +
				          Rml::StringUtilities::EncodeRml(app.join_page) + "\"/>" +
				          button("game.qr-client", "Show console QR") + button("game.qr-gm", "Show GM QR") + "</div>";
		}
		if (!notice.empty())
			description += " " + notice;
		if (!app.status.empty())
			description += " " + app.status;
		app.accessible_status = description;
		auto *game = document.GetElementById("game");
		auto *session_controls = document.GetElementById("session-controls");
		game_top = app.screen == Screen::viewscreen
		               ? 0.f
		               : std::max(200.f, session_controls->GetAbsoluteTop() + session_controls->GetOffsetHeight() + 12);
		document.GetElementById("description")
		    ->SetProperty("width", std::to_string(std::max(40, GetScreenWidth() - 40)) + "px");
		session_controls->SetProperty("width", std::to_string(std::max(40, GetScreenWidth() - 40)) + "px");
		game->SetProperty("width", std::to_string(std::max(40, GetScreenWidth() - 40)) + "px");
		game->SetProperty("top", std::to_string(static_cast<int>(game_top)) + "px");
		game->SetProperty("height",
		                  std::to_string(std::max(80, GetScreenHeight() - static_cast<int>(game_top) - 20)) + "px");
		if (!markup_ready || markup != previous_markup)
		{
			game->SetInnerRML(markup);
			previous_markup = markup;
		}
		if (!markup_ready || network_markup != previous_network)
		{
			document.GetElementById("session-controls")->SetInnerRML(network_markup);
			previous_network = network_markup;
		}
		const auto displayed = Rml::StringUtilities::EncodeRml(label(app, description));
		if (!markup_ready || displayed != previous_description)
			document.GetElementById("description")->SetInnerRML(displayed);
		previous_description = displayed;
		markup_ready = true;
	}
	const SceneFrame &prepare_scene(Application &app)
	{
		bind_session(app);
		frame.visible.clear();
		frame.focus_body.reset();
		if (!app.session || !app.session->running() || app.screen == Screen::workshop ||
		    (!app.can_host && app.screen != Screen::viewscreen && !app.viewscreen_region))
		{
			motion.reset();
			previous_handling_pose.reset();
			frame.bodies.clear();
			frame.shared_bodies.reset();
			return frame;
		}
		const auto scene_data_started = GetTime();
		const auto scene_snapshot_started = GetTime();
		frame.shared_bodies = app.session->scene_snapshot();
		if (!frame.shared_bodies)
			frame.bodies = app.session->scene();
		app.frame_metrics["scene-snapshot"] = GetTime() - scene_snapshot_started;
		const auto previous_focus = environment.own;
		environment.own = app.session->focus_body();
		if (!environment.own && previous_focus)
			for (const auto &body : frame.view())
				if (body.audio_only && body.id == previous_focus->id)
					environment.own = static_cast<const BodyView &>(body);
		const auto physical_own = environment.own;
		motion.sample(app.session->ticks(), GetTime(), frame.view(), physical_own);
		frame.shared_bodies.reset();
		motion.render(GetTime(), frame.bodies, environment.own);
		frame.focus_body = environment.own;
		if (!preload_finished)
		{
			// Audio and permitted scene facts continue while graphics are pending,
			// including a host that intentionally shows only Console displays.
			if (environment.own)
				frame.visible.insert(environment.own->id);
			return frame;
		}
		environment.dust_enabled = app.dust_motes;
		environment.grid_enabled = app.reference_grid;
		environment.reduced_motion = app.reduced_motion;
		const auto presentation = app.session->scene_presentation();
		const auto definition = environment.own ? environment.own->definition : std::string{};
		if (!environment_ready || presentation_identity != presentation || grid_definition != definition)
		{
			environment.skybox = presentation.value("skybox", std::string{});
#ifdef __EMSCRIPTEN__
			constexpr bool web = true;
#else
			constexpr bool web = false;
#endif
			environment.dust = compile_dust(presentation.value("dust", Json::object()), app.session->catalog(), web,
			                                "presentation.dust");
			environment.grid.reset();
			if (!definition.empty())
			{
				const auto &render = app.session->catalog().entities.at(definition).at("render");
				camera_aspect = 1;
				if (render.contains("sprite"))
				{
					const auto sprite = scene.prepared_sprite(app.session->catalog(), definition);
					const auto frames = [&](const auto &values)
					{
						for (const auto &frame : values)
							camera_aspect = std::max(camera_aspect, static_cast<float>(frame.rect[2]) / frame.rect[3]);
					};
					frames(sprite.frames);
					for (const auto &entry : sprite.entries)
						frames(entry.frames);
				}
				else if (render.contains("images"))
					for (const auto &reference : render.at("images"))
					{
						const auto id = reference.get<std::string>();
						const auto texture = textures.load(id, app.session->catalog().assets.at(id));
						camera_aspect = std::max(camera_aspect, static_cast<float>(texture.width) / texture.height);
					}
				if (render.contains("reference_grid"))
					environment.grid = compile_reference_grid(render.at("reference_grid"), "render.reference_grid");
			}
			presentation_identity = presentation;
			grid_definition = definition;
			environment_ready = true;
		}
		if (!environment.own || !physical_own)
		{
			previous_handling_pose.reset();
			return frame;
		}
		const auto &own = *environment.own;
		const auto settings = ship_camera_state(app.session->public_board());
		const auto scale = app.session->catalog().entities.at(own.definition).at("render").at("scale").get<float>();
		const auto pose =
		    ship_camera_pose(own.x, own.y, own.heading, scale, camera_aspect, settings.mode, settings.direction);
		Camera3D camera{{static_cast<float>(pose.position[0]), static_cast<float>(pose.position[1]),
		                 static_cast<float>(pose.position[2])},
		                {static_cast<float>(pose.target[0]), static_cast<float>(pose.target[1]),
		                 static_cast<float>(pose.target[2])},
		                {0, 1, 0},
		                50,
		                CAMERA_PERSPECTIVE};
		app.frame_metrics["scene-data"] = GetTime() - scene_data_started;
		const auto tick = app.session->ticks();
		const float dt = std::clamp(GetFrameTime(), 0.f, .05f);
		auto own_scene = std::find_if(frame.bodies.begin(), frame.bodies.end(),
		                              [&](const SceneBody &body)
		                              {
			                              return body.id == own.id && !body.audio_only;
		                              });
		const int thrust = own_scene == frame.bodies.end() ? 0 : own_scene->forward_thrust;
		const float load = std::clamp(thrust / 1000.f, 0.f, 1.f);
		if (!previous_handling_pose || previous_handling_pose->id != own.id || tick < handling_tick)
		{
			previous_handling_pose = physical_own;
			handling_tick = tick;
			acceleration = camera_offset = {};
			plume_level = load;
			burning = thrust > 0;
			ignition = 0;
		}
		else if (tick > handling_tick)
		{
			const float seconds = static_cast<float>(tick - handling_tick) / 20.f;
			acceleration = {
			    static_cast<float>((double(physical_own->vx) - previous_handling_pose->vx) / (1000.0 * seconds)),
			    static_cast<float>((double(physical_own->vy) - previous_handling_pose->vy) / (1000.0 * seconds))};
			previous_handling_pose = physical_own;
			handling_tick = tick;
		}
		if (app.thrust_camera && !app.reduced_motion)
		{
			const float cap = scale * .025f;
			const float gain = std::min(scale * .004f, cap / std::max(1.f, std::hypot(acceleration.x, acceleration.y)));
			const float blend = 1.f - std::exp(-8.f * dt);
			camera_offset.x += (-acceleration.x * gain - camera_offset.x) * blend;
			camera_offset.y += (acceleration.y * gain - camera_offset.y) * blend;
			camera.position.x += camera_offset.x;
			camera.position.z += camera_offset.y;
			camera.target.x += camera_offset.x;
			camera.target.z += camera_offset.y;
		}
		else
			camera_offset = {};

		// A short flare on ignition and a softer tail after the actual thrust falls away.
		if (app.thrust_transients && !app.reduced_motion && own_scene != frame.bodies.end())
		{
			ignition *= std::exp(-13.f * dt);
			if (thrust > 0 && !burning)
				ignition = std::min(.45f, load * .75f);
			const float response = load > plume_level ? 24.f : 11.f;
			plume_level += (load - plume_level) * (1.f - std::exp(-response * dt));
			const float plume = std::min(1.f, plume_level + ignition);
			own_scene->forward_thrust = plume >= .025f ? static_cast<int>(plume * 1000.f) : 0;
		}
		else
		{
			plume_level = load;
			ignition = 0;
		}
		burning = thrust > 0;
		frame_camera = camera;
		frame_lighting = app.session->scene_lighting();
		frame_time = GetTime();
		const auto r = app.viewscreen_region;
		const Rectangle region =
		    r ? Rectangle{r->x, r->y, r->width, r->height}
		      : Rectangle{0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
		const auto scene_prepare_started = GetTime();
		frame.visible =
		    scene.prepare(app.session->catalog(), frame.view(), camera, region, settings.mode == 0 ? own.id : 0,
		                  frame_lighting.value("environment", Json::object()).value("simulation_time", frame_time),
		                  environment.reduced_motion);
		// Audio and subsequent consumers receive the original permitted thrust sample.
		if (own_scene != frame.bodies.end())
			own_scene->forward_thrust = thrust;
		app.frame_metrics["scene-visibility"] = GetTime() - scene_prepare_started;
		// Cockpit feedback remains audible when the camera hides the host hull.
		frame.visible.insert(own.id);
		return frame;
	}
	void draw(const Application &app)
	{
		if (!app.session)
			return;
		if (show_invitation && invitation.id && (app.screen == Screen::home || app.screen == Screen::server_lobby))
		{
			const int modules = invitation.width / 4;
			const int scale = std::max(1, std::min((GetScreenWidth() - 40) / modules,
			                                       (GetScreenHeight() - static_cast<int>(game_top) - 90) / modules));
			DrawTextureEx(invitation, {20, game_top + 70}, 0, scale / 4.f, WHITE);
			return;
		}
		if (!app.session->running())
			return;
		if (app.screen == Screen::viewscreen || display_region)
		{
			if (!environment.own)
				return;
			const auto stage = GetTime();
			scene.draw_prepared(app.session->catalog(), frame.view(), frame_camera, area(), frame_lighting, frame_time,
			                    SceneDiagnostic::comic, {}, environment, 0u);
			scene_metrics["scene_render"] = GetTime() - stage;
			const auto shared = app.session->public_board().value("display", Json(nullptr));
			if (shared.is_object())
			{
				const auto rect = area();
				DrawRectangleRec(rect, {0, 0, 0, 166});
				const Rectangle plot{rect.x + rect.width * .04f, rect.y + rect.height * .08f, rect.width * .92f,
				                     rect.height * .84f};
				const auto own = display::read_body(shared.at("own"));
				if (shared.at("kind") == "conversations")
				{
					// The RmlUi overlay below supplies shaped, accessible transcript text.
				}
				else if (shared.at("kind") == "navigation")
				{
					NavigationCamera camera;
					camera.x = shared.at("camera")[0];
					camera.y = shared.at("camera")[1];
					camera.scale = shared.at("camera")[2].get<double>() * 100 / plot.width;
					draw_navigation_plot(own, shared.at("regions"), Json::array(), Json::object(), camera, plot, true);
					BeginScissorMode(static_cast<int>(plot.x), static_cast<int>(plot.y), static_cast<int>(plot.width),
					                 static_cast<int>(plot.height));
					for (const auto &row : shared.at("contacts"))
					{
						const auto contact = display::read_body(row);
						const Vector2 at{
						    plot.x + plot.width / 2 + static_cast<float>((contact.x - camera.x) / camera.scale),
						    plot.y + plot.height / 2 - static_cast<float>((contact.y - camera.y) / camera.scale)};
						draw_radar_body(textures, app.session->catalog(), contact, at, 9, SKYBLUE);
					}
					EndScissorMode();
				}
				else
				{
					Observation view{own, {}};
					view.regions = shared.at("regions");
					view.bearing_contacts = projection::read_bearings(shared.value("bearing_contacts", Json::array()));
					auto sensor = display::read_radar(shared.at("radar"));
					for (const auto &b : view.bearing_contacts)
						sensor.contact_ids.push_back(b.id);
					for (const auto &contact : shared.at("contacts"))
					{
						view.contacts.push_back(display::read_body(contact));
						sensor.contact_ids.push_back(view.contacts.back().id);
					}
					auto &sweep = sweeps["shared/" + shared.at("console").get<std::string>() + "/" +
					                     shared.at("widget").get<std::string>()];
					if (!sweep)
						sweep = std::make_unique<RadarSweep>();
					draw_radar_plot(textures, app.session->catalog(), view, sensor, Json::object(), *sweep,
					                shared.at("sweep"), {plot.x + plot.width / 2, plot.y + plot.height / 2},
					                std::min(plot.width, plot.height) * .48f, GetTime(), app.reduced_motion);
				}
			}
			if (!viewscreen_frame.id)
			{
#ifdef __EMSCRIPTEN__
				const std::string path = "/assets/helm/viewscreen-frame.png";
#else
				const std::string path = std::string(GetApplicationDirectory()) + "assets/helm/viewscreen-frame.png";
#endif
				viewscreen_frame = textures.load_file(path);
			}
			if (viewscreen_frame.id)
			{
				const auto rect = area();
				const auto chrome = viewscreen_chrome(rect);
				const auto scale = chrome.scale;
				const float fw = static_cast<float>(viewscreen_frame.width);
				const float fh = static_cast<float>(viewscreen_frame.height);
				if (!app.session->public_board().value("alert", Json::object()).empty())
				{
					// The condition label and this raised section share the same physical bounds.
					const auto panel = chrome.condition_frame;
					DrawRectangleRec(
					    {panel.x, panel.y + 12 * scale, panel.width - 20 * scale, panel.height - 12 * scale},
					    {8, 13, 15, 255});
					DrawTexturePro(viewscreen_frame, {112, 0, fw - 224, 40},
					               {panel.x, panel.y, panel.width - 56 * scale, 20 * scale}, {0, 0}, 0, WHITE);
					DrawTexturePro(viewscreen_frame, {fw - 112, 0, 112, 112},
					               {panel.x + panel.width - 56 * scale, panel.y, 56 * scale, 56 * scale}, {0, 0}, 0,
					               WHITE);
					DrawTexturePro(viewscreen_frame, {fw - 40, 112, 40, fh - 224},
					               {panel.x + panel.width - 20 * scale, panel.y + 56 * scale, 20 * scale,
					                panel.height - 56 * scale},
					               {0, 0}, 0, WHITE);
				}
				// Scale all eight border slices; raylib's NPatch keeps corners at source-pixel size.
				const float sx[]{0, 112, fw - 112, fw}, sy[]{0, 112, fh - 112, fh};
				const float dx[]{rect.x, rect.x + chrome.corner, rect.x + rect.width - chrome.corner,
				                 rect.x + rect.width};
				const float dy[]{rect.y, rect.y + chrome.corner, rect.y + rect.height - chrome.corner,
				                 rect.y + rect.height};
				for (int row = 0; row < 3; ++row)
					for (int column = 0; column < 3; ++column)
						if (row != 1 || column != 1)
							DrawTexturePro(viewscreen_frame,
							               {sx[column], sy[row], sx[column + 1] - sx[column], sy[row + 1] - sy[row]},
							               {dx[column], dy[row], dx[column + 1] - dx[column], dy[row + 1] - dy[row]},
							               {0, 0}, 0, WHITE);
			}
		}
		else if (app.screen == Screen::console)
		{
			const auto *console = console_definition(app);
			if (!observation || !console)
				return;
			const auto &systems = app.session->catalog().entities.at(observation->own.definition).at("systems");
			for (const auto &widget : console->at("widgets"))
				if (widget.at("kind") == "radar")
				{
					auto rect = widget_rect(widget);
					rect.y += 100;
					rect.height -= 110;
					if (rect.width < 10 || rect.height < 10)
						continue;
					BeginScissorMode(static_cast<int>(rect.x), static_cast<int>(rect.y), static_cast<int>(rect.width),
					                 static_cast<int>(rect.height));
					Vector2 centre{rect.x + rect.width / 2, rect.y + rect.height / 2};
					float radius = std::min(rect.width, rect.height) * 0.45f;
					for (int ring = 1; ring <= 4; ++ring)
						DrawCircleLinesV(centre, radius * ring / 4, Color{45, 91, 111, 255});
					DrawLineV({centre.x - radius, centre.y}, {centre.x + radius, centre.y}, Color{45, 91, 111, 255});
					DrawLineV({centre.x, centre.y - radius}, {centre.x, centre.y + radius}, Color{45, 91, 111, 255});
					draw_radar_body(textures, app.session->catalog(), observation->own, centre, 9, WHITE);
					const auto name = widget.value("system", std::string{});
					const auto sensor =
					    std::find_if(observation->systems.begin(), observation->systems.end(),
					                 [&](const auto &s)
					                 {
						                 return s.kind == OperatingSystem::Radar && (name.empty() || s.id == name);
					                 });
					const double range =
					    sensor != observation->systems.end() ? std::max<std::int64_t>(1, sensor->maximum) : 1;
					if (sensor != observation->systems.end())
					{
						auto &sweep = sweeps[widget.at("id").get<std::string>()];
						if (!sweep)
							sweep = std::make_unique<RadarSweep>();
						sweep->draw(centre, radius, *sensor, widget.value("sweep", Json::object()), GetTime(),
						            app.reduced_motion);
						draw_region_markers(
						    observation->regions,
						    [&](double x, double y)
						    {
							    return Vector2{centre.x + static_cast<float>((x - observation->own.x) / range) * radius,
							                   centre.y -
							                       static_cast<float>((y - observation->own.y) / range) * radius};
						    },
						    range / radius, &*sensor, centre, radius);
					}
					if (sensor != observation->systems.end())
						draw_bearing_ring(*observation, *sensor, centre, radius);
					for (const auto &contact : observation->contacts)
					{
						if (sensor == observation->systems.end() ||
						    std::find(sensor->contact_ids.begin(), sensor->contact_ids.end(), contact.id) ==
						        sensor->contact_ids.end())
							continue;
						Vector2 point{centre.x + static_cast<float>((contact.x - observation->own.x) / range) * radius,
						              centre.y - static_cast<float>((contact.y - observation->own.y) / range) * radius};
						draw_radar_body(textures, app.session->catalog(), contact, point, 7, SKYBLUE);
						DrawText(TextFormat("#%llu", static_cast<unsigned long long>(contact.id)),
						         static_cast<int>(point.x + 10), static_cast<int>(point.y), 14, WHITE);
					}
					EndScissorMode();
				}
		}
	}
	void close_resources()
	{
		textures.clear();
		viewscreen_frame = {};
		scene.clear();
		if (invitation.id)
			UnloadTexture(invitation);
		invitation = {};
		show_invitation = false;
		invitation_url.clear();
	}
};
GameScreen::GameScreen() : state_(std::make_unique<State>())
{
}
GameScreen::~GameScreen() = default;
GameScreen::Loading GameScreen::preload(Application &app)
{
	auto &s = *state_;
	s.bind_session(app);
	const auto route = app.viewscreen_region ? app.viewscreen_region->console : 0;
	const auto identity =
	    Json(s.content_identity).dump() + "/" + s.resource_mission + (app.content_action ? "/content" : "/scene");
	const auto request = s.scene_loading.begin(route, identity);
	if (s.scene_loading.progress().state != Loading::State::preparing)
		return s.scene_loading.progress();
	auto progress = preload_step(app);
	s.scene_loading.advance(progress.completed, progress.total, progress.phase);
	if (!progress.error.empty())
		s.scene_loading.fail(request, progress.error);
	else if (!progress.active)
		s.scene_loading.complete(request);
	progress.target = route;
	progress.request = request;
	progress.state = s.scene_loading.progress().state;
	return progress;
}
bool GameScreen::scene_ready() const
{
	return state_->preload_finished;
}
GameScreen::Loading GameScreen::preload_console(Application &app, ConsoleId route)
{
	auto &p = state_->console_preparation[route];
	if (!app.session)
		return {};
	const auto &catalog = app.session->catalog();
	const auto source = app.session->console_snapshot(route);
	std::string ship = source ? source->own.definition : std::string{};
	if (ship.empty())
	{
		const auto lobby = app.session->lobby();
		for (const auto &seat : lobby.consoles)
			if (seat.local_screen == route)
				for (const auto &slot : lobby.slots)
					if (slot.id == seat.slot)
						ship = slot.ship;
	}
	if (ship.empty())
	{
		if (app.session->content_progress())
		{
			p.loading.begin(route, "receiving/" + Json(catalog.ordered_packs).dump());
			p.loading.advance(0, 0, "Receiving Console assets");
			return p.loading.progress();
		}
		// Claim/reconnection UI still needs its ordinary preparation. Retain the
		// previous asset owner until an actual replacement ship/content is known.
		return {};
	}
	const auto identity = Json(catalog.ordered_packs).dump() + "/" + ship;
	const auto request = p.loading.begin(route, identity);
	if (p.identity != identity)
	{
		p.identity = identity;
		p.textures.clear();
		p.images.clear();
		p.ui_images.clear();
		p.files.clear();
		p.done = 0;
	}
	if (p.loading.progress().state != Loading::State::preparing)
		return p.loading.progress();
	try
	{
		if (ship.empty() || !catalog.entities.contains(ship))
		{
			p.loading.advance(0, 0, "Receiving Console assets");
			return p.loading.progress();
		}
		if (p.files.empty())
		{
			p.images = console_image_assets(catalog, ship);
			p.ui_images = console_image_assets(catalog, ship, true);
#ifdef __EMSCRIPTEN__
			const std::string ui_root = "/assets/helm/";
#else
			const std::string ui_root = std::string(GetApplicationDirectory()) + "assets/helm/";
#endif
			for (const auto *name : {"helm-background-9slice", "readout-panel", "heading-pointer", "helm-radar-ring",
			                         "helm-throttle-track", "helm-throttle-handle"})
				p.files.push_back(ui_root + name + ".png");
			p.files.push_back(ui_root + "../menu/helm-background-9slice.png");
			p.files.push_back(ui_root + "../menu/button-generic-9slice.png");
		}
		const auto total = p.images.size() + p.files.size() + p.ui_images.size();
		p.loading.advance(p.done, total, "Loading cockpit controls");
		const auto started = GetTime();
		while (p.done < total)
		{
			bool ready = false;
			if (p.done < p.images.size())
			{
				const auto &image = p.images.at(p.done);
				ready = p.textures.prepare(image, catalog.assets.at(image));
			}
			else if (p.done < p.images.size() + p.files.size())
			{
				const auto index = p.done - p.images.size();
				ready = p.textures.prepare_file(p.files.at(index), index >= 6);
			}
			else
			{
				const auto &image = p.ui_images.at(p.done - p.images.size() - p.files.size());
				ready = p.textures.prepare(image, catalog.assets.at(image), true);
			}
			if (!ready)
				break;
			++p.done;
			if (GetTime() - started >= .002)
				break;
		}
		if (p.done == total)
			p.loading.complete(request);
		p.loading.advance(p.done, total, "Loading cockpit controls");
	}
	catch (const std::exception &error)
	{
		p.loading.fail(request, error.what());
	}
	return p.loading.progress();
}
bool GameScreen::loading_action(ConsoleId route, std::uint64_t request, bool retry)
{
	SurfaceLoad *loading = &state_->scene_loading;
	if (route != loading->progress().target)
	{
		const auto found = state_->console_preparation.find(route);
		if (found == state_->console_preparation.end())
			return false;
		loading = &found->second.loading;
	}
	if (!loading->progress().active || loading->progress().request != request)
		return false;
	if (retry)
	{
		loading->retry();
		if (loading == &state_->scene_loading)
			state_->preload_error.clear();
		return true;
	}
	return loading->cancel(request);
}
GameScreen::Loading GameScreen::console_preparation_failed(ConsoleId route, const std::string &error)
{
	auto &loading = state_->console_preparation.at(route).loading;
	loading.retry();
	loading.fail(loading.progress().request, error);
	loading.advance(loading.progress().completed, loading.progress().total, "Loading failed");
	return loading.progress();
}
GameScreen::Loading GameScreen::preload_step(Application &app)
{
	auto &s = *state_;
	if (app.content_action)
		return {true, 0, 0, "Loading game packs", {}, app.content_error};
	s.bind_session(app);
	if (!app.session)
		return {};
	if (const auto phase = app.session->preparation_phase(); !phase.empty())
		return {true, 0, 0, phase, {}, {}};
	if (const auto progress = app.session->content_progress())
		return {true,
		        static_cast<std::size_t>(progress->first),
		        static_cast<std::size_t>(progress->second),
		        "Receiving Console assets",
		        {},
		        {}};
	const auto &catalog = app.session->catalog();
	const auto mission = app.session->loading_scenario();
	const bool eligible = catalog.presentation || (!mission.empty() && catalog.maps.contains(mission));
	if (!eligible || s.preload_finished)
		return {};
	const auto title = mission.empty() ? localise("Console") : catalog.maps.at(mission).value("name", mission);
	if (!s.preload_error.empty())
		return {true,
		        s.preload_done,
		        s.preload_images.size() + s.preload_definitions.size() + s.preload_ui.size() + 1,
		        "Loading failed",
		        title,
		        s.preload_error};
	try
	{
		if (!s.preload_planned)
		{
			if (catalog.presentation)
				for (const auto &[id, bytes] : catalog.assets)
					s.preload_images.push_back(id);
			else
			{
				s.preload_images = scenario_assets(catalog, mission);
				for (const auto &id : scenario_definitions(catalog, mission))
					if (catalog.entities.contains(id))
						s.preload_definitions.push_back(id);
			}
#ifdef __EMSCRIPTEN__
			const std::string ui_root = "/assets/helm/";
#else
			const std::string ui_root = std::string(GetApplicationDirectory()) + "assets/helm/";
#endif
			for (const auto *name : {"helm-background-9slice", "readout-panel", "heading-pointer", "helm-radar-ring",
			                         "helm-throttle-track", "helm-throttle-handle"})
				s.preload_ui.push_back(ui_root + name + ".png");
			if (!catalog.presentation)
				s.preload_ui.push_back(ui_root + "viewscreen-frame.png");
			s.preload_planned = true;
			return {true,
			        0,
			        s.preload_images.size() + s.preload_definitions.size() + s.preload_ui.size() + 1,
			        "Preparing assets",
			        title,
			        {}};
		}
		const auto total = s.preload_images.size() + s.preload_definitions.size() + s.preload_ui.size() + 1;
		std::string phase;
		// Yield to the platform after each bounded resource; GPU work stays on the graphics thread.
		if (s.preload_done < s.preload_images.size())
		{
			phase = "Loading ship and level images";
			const auto &id = s.preload_images.at(s.preload_done);
			const auto &bytes = catalog.assets.at(id);
			if (compiled_image(bytes) ||
			    (bytes.size() >= 4 && bytes[0] == 137 && bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71))
			{
				if (!s.textures.prepare(id, bytes))
					return {true, s.preload_done, total, phase, title, {}};
			}
			++s.preload_done;
		}
		else if (s.preload_done < s.preload_images.size() + s.preload_definitions.size())
		{
			phase = "Preparing ships and scenery";
			s.scene.preload(catalog, s.preload_definitions.at(s.preload_done - s.preload_images.size()));
			++s.preload_done;
		}
		else if (s.preload_done < total - 1)
		{
			phase = "Loading cockpit controls";
			if (!s.textures.prepare_file(
			        s.preload_ui.at(s.preload_done - s.preload_images.size() - s.preload_definitions.size())))
				return {true, s.preload_done, total, phase, title, {}};
			++s.preload_done;
		}
		else if (s.preload_done == total - 1)
		{
			phase = "Preparing space backdrop and lighting";
			if (!catalog.presentation)
			{
				SceneEnvironment environment;
				environment.own = BodyView{};
#ifdef __EMSCRIPTEN__
				constexpr bool web = true;
#else
				constexpr bool web = false;
#endif
				const auto &map = catalog.maps.at(mission);
				environment.skybox = map.value("presentation", Json::object()).value("skybox", std::string{});
				environment.dust =
				    compile_dust(map.value("presentation", Json::object()).value("dust", dust_defaults()), catalog, web,
				                 "loading.dust");
				environment.grid = compile_reference_grid(reference_grid_defaults(), "loading.grid");
				s.scene.preload_environment(catalog, environment);
			}
			++s.preload_done;
		}
		else
		{
			s.preload_finished = true;
			return {};
		}
		return {true, s.preload_done, total, phase, title, {}};
	}
	catch (const std::exception &error)
	{
		s.preload_error = error.what();
		return {true,
		        s.preload_done,
		        s.preload_images.size() + s.preload_definitions.size() + s.preload_ui.size() + 1,
		        "Loading failed",
		        title,
		        s.preload_error};
	}
}
void GameScreen::refresh_status(Application &app)
{
	state_->refresh_status(app);
}
void GameScreen::prepare(Application &app, Rml::ElementDocument &document)
{
	state_->prepare(app, document);
}
const SceneFrame &GameScreen::prepare_scene(Application &app)
{
	return state_->prepare_scene(app);
}
void GameScreen::draw(const Application &app)
{
	if (!state_->preload_finished && (app.screen == Screen::viewscreen || app.screen == Screen::console))
		return;
	if (app.screen == Screen::viewscreen && app.viewscreen_region)
	{
		const auto &r = *app.viewscreen_region;
		state_->display_region = Rectangle{r.x, r.y, r.width, r.height};
	}
	state_->draw(app);
	state_->display_region.reset();
}
void GameScreen::draw_viewscreen(Application &app, Rectangle region)
{
	if (!state_->preload_finished)
		return;
	state_->display_region = region;
	state_->draw(app);
	for (const auto &[name, duration] : state_->scene_metrics)
		app.frame_metrics[name] = duration;
	state_->display_region.reset();
}
} // namespace phoenix
