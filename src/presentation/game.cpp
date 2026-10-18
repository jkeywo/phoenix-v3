module;
#include <raylib.h>
#include <rlgl.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <qrcodegen.hpp>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core.h>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.presentation;
import phoenix.application;
import phoenix.console;
import phoenix.files;
import phoenix.session.api;
#include "game.hpp"

namespace phoenix
{
TextureCache::~TextureCache()
{
	clear();
}
void TextureCache::clear()
{
	for (const auto &[id, texture] : textures_)
		UnloadTexture(texture);
	textures_.clear();
}
Texture2D TextureCache::load(const std::string &id, std::span<const std::uint8_t> png)
{
	if (const auto found = textures_.find(id); found != textures_.end())
		return found->second;
	const auto image = LoadImageFromMemory(".png", png.data(), static_cast<int>(png.size()));
	if (!image.data)
		return {};
	const auto texture = LoadTextureFromImage(image);
	UnloadImage(image);
	if (texture.id)
		textures_.emplace(id, texture);
	return texture;
}
struct GameScreen::State
{
	FileImportInbox imports;
	std::string mission, slot, ship, seat, notice;
	ConsoleModel console_model;
	TextureCache textures;
	SceneRenderer scene;
	Texture2D invitation{};
	std::string invitation_url;
	bool show_invitation = false;
	float azimuth = 0.7f, elevation = 0.8f, distance = 160;
	Vector3 focus{};
	float game_top = 200;
	std::optional<Rectangle> display_region;
	std::map<std::string, double> scene_metrics;
	std::optional<std::uint64_t> save_after;
	std::weak_ptr<SessionPort> owner;
	std::uint64_t generation = 0;
	std::vector<std::pair<std::string, std::string>> content_identity;
	std::optional<Observation> observation;
	bool was_running = false;
	std::string previous_markup, previous_description, previous_network;
	bool markup_ready = false;
	static constexpr float pi = 3.14159265358979323846f;
	~State()
	{
		close_resources();
	}
	void reset_session()
	{
		imports.reset();
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
		azimuth = 0.7f;
		elevation = 0.8f;
		distance = 160;
		focus = {};
		markup_ready = false;
	}
	void bind_session(const Application &app)
	{
		const decltype(content_identity) empty;
		const auto &packs = app.session ? app.session->catalog().ordered_packs : empty;
		const auto current_generation = app.session ? app.session->generation() : 0;
		if (owner.lock() != app.session || generation != current_generation || content_identity != packs)
		{
			reset_session();
			owner = app.session;
			generation = current_generation;
			content_identity = packs;
		}
	}
	std::string escape(std::string value)
	{
		std::string result;
		for (char c : value)
		{
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
		}
		return result;
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
		                 "&code=" + query(app.session->invitation_code()) + "&service=" + query(app.network_service);
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
				if (auto own = session.focus_body())
				{
					focus = {own->x / 1000.f, 0, -own->y / 1000.f};
				}
				notice = "Session started.";
				app.screen = Screen::viewscreen;
			}
			else if (id == "game.console" && session.running())
				app.screen = Screen::console;
			else if (id == "game.scene" && session.running())
				app.screen = Screen::viewscreen;
			else if (id == "game.local-gm" && session.running() && app.can_host)
				app.screen = Screen::gm;
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
			app.screen_controls.emplace_back(id, text);
			return "<button id=\"" + escape(id) + "\">" + escape(label(app, text)) + "</button>";
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
					export_file("phoenix-session.zip", app.session->save());
					notice = "Runtime save exported with its exact selected packs.";
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
			observation = console_model.observation();
			const auto focus_body =
			    observation ? std::optional(observation->own)
			                : (app.can_host && app.session->networked() ? app.session->focus_body() : std::nullopt);
			if (app.session->running() && !was_running)
			{
				if (auto own = focus_body)
				{
					focus = {own->x / 1000.f, 0, -own->y / 1000.f};
					const auto &render = catalog.entities.at(own->definition).at("render");
					if (render.contains("sprite"))
					{
						distance = std::clamp(render.at("scale").get<float>() * 4.5f, 30.f, 2000000.f);
						elevation = .22f;
						azimuth = -(own->heading / 1000.f + 90) * pi / 180;
					}
				}
				close_resources();
			}
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
				else
					markup = button("game.scene", "Open viewscreen") + button("game.console", "Open ship console") +
					         button("game.save", "Save runtime ZIP") + button("game.load", "Load runtime save") +
					         button("game.clear", "End session and clear packs");
			}
			else if ((app.screen == Screen::viewscreen || app.screen == Screen::console) && app.session->running())
			{
				description = "Tick " + std::to_string(app.session->ticks());
				if (auto own = focus_body)
					description += " / position " + std::to_string(own->x / 1000) + ", " +
					               std::to_string(own->y / 1000) + " m / speed " +
					               std::to_string(static_cast<int>(
					                   std::hypot(static_cast<double>(own->vx), static_cast<double>(own->vy)) / 1000)) +
					               " m/s / heading " + std::to_string(own->heading / 1000) + " degrees.";
				if (app.screen == Screen::viewscreen)
					description += " Right drag: orbit; middle drag: pan; wheel: zoom.";
				if (app.screen == Screen::viewscreen)
					markup = button("game.console", "Open ship console");
				else if (const auto *console = console_definition(app))
				{
					for (const auto &widget : console->at("widgets"))
					{
						const auto percent = [&](const char *name)
						{
							return std::to_string(widget.at(name).get<double>() / 10) + "%";
						};
						markup += "<div class=\"widget\" style=\"left:" + percent("x") + ";top:" + percent("y") +
						          ";width:" + percent("width") + ";height:" + percent("height") + "\"><h3>" +
						          escape(label(app, widget.at("label"))) + "</h3>";
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
							markup += "<p>" + escape(notice) + "</p>";
						markup += "</div>";
					}
				}
			}
			if (app.session->networked())
			{
				description += " " + app.session->connection_status();
				if (app.screen != Screen::gm && app.screen != Screen::workshop)
					for (const auto &[id, text] : app.session->lobby_controls())
						network_markup += button(id, text);
			}
		}
		if (app.can_host && app.session && app.session->running())
			markup += button("game.local-gm", "Open local GM inspector");
		if (app.screen == Screen::client_lobby && app.connect && !observation)
		{
			markup = "<div class=\"connection\"><label>Network code (room/server)</label><input id=\"join-code\" "
			         "type=\"text\" value=\"" +
			         escape(app.network_code) +
			         "\"/><label>Network service</label><input id=\"join-service\" type=\"text\" value=\"" +
			         escape(app.network_service) +
			         "\"/><label>Local profile</label><input id=\"join-profile\" type=\"text\" value=\"" +
			         escape(app.network_profile) + "\"/>" + button("game.connect-client", "Connect console") +
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
				          escape(app.join_page) + "\"/>" + button("game.qr-client", "Show console QR") +
				          button("game.qr-gm", "Show GM QR") + "</div>";
		}
		if (!notice.empty())
			description += " " + notice;
		if (!app.status.empty())
			description += " " + app.status;
		app.accessible_status = description;
		auto *game = document.GetElementById("game");
		auto *session_controls = document.GetElementById("session-controls");
		game_top = std::max(200.f, session_controls->GetAbsoluteTop() + session_controls->GetOffsetHeight() + 12);
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
		const auto displayed = escape(label(app, description));
		if (!markup_ready || displayed != previous_description)
			document.GetElementById("description")->SetInnerRML(displayed);
		previous_description = displayed;
		markup_ready = true;
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
			if (CheckCollisionPointRec(GetMousePosition(), area()))
			{
				auto delta = GetMouseDelta();
				if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
				{
					azimuth -= delta.x * 0.005f;
					elevation = std::clamp(elevation + delta.y * 0.005f, 0.15f, 1.5f);
				}
				if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
				{
					focus.x -= delta.x * distance / 700;
					focus.z -= delta.y * distance / 700;
				}
				distance = std::clamp(distance * std::pow(0.9f, GetMouseWheelMove()), 5.f, 2000000.f);
			}
			Camera3D camera{{focus.x + distance * std::cos(elevation) * std::cos(azimuth),
			                 distance * std::sin(elevation),
			                 focus.z + distance * std::cos(elevation) * std::sin(azimuth)},
			                focus,
			                {0, 1, 0},
			                50,
			                CAMERA_PERSPECTIVE};
			const auto stage = GetTime();
			const auto bodies = app.session->scene();
			scene.draw(app.session->catalog(), bodies, camera, area(), app.session->scene_lighting(), GetTime());
			scene_metrics["scene_render"] = GetTime() - stage;
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
					draw_radar_icon(observation->own.radar_icon, centre, 9,
					                static_cast<float>(observation->own.heading), WHITE);
					const double range =
					    systems.contains("radar") ? systems.at("radar").at("range_mm").get<double>() : 1;
					for (const auto &contact : observation->contacts)
					{
						Vector2 point{centre.x + static_cast<float>((contact.x - observation->own.x) / range) * radius,
						              centre.y - static_cast<float>((contact.y - observation->own.y) / range) * radius};
						draw_radar_icon(contact.radar_icon, point, 7, static_cast<float>(contact.heading), SKYBLUE);
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
void GameScreen::prepare(Application &app, Rml::ElementDocument &document)
{
	state_->prepare(app, document);
}
void GameScreen::draw(const Application &app)
{
	state_->draw(app);
}
void GameScreen::draw_viewscreen(Application &app, Rectangle region)
{
	state_->display_region = region;
	state_->draw(app);
	for (const auto &[name, duration] : state_->scene_metrics)
		app.frame_metrics[name] = duration;
	state_->display_region.reset();
}
} // namespace phoenix
