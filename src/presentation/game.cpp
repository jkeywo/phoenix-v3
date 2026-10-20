module;
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#endif
#include "ship_camera.hpp"
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
#include <string>
#include <utility>
#include <vector>

module phoenix.presentation;
import phoenix.application;
import phoenix.console;
import phoenix.files;
import phoenix.session.api;
#include "game.hpp"
#include "region_markers.hpp"
#include "navigation_camera.hpp"
#include "map_plots.hpp"
#include "../core/display_projection.hpp"

namespace phoenix
{
Image load_asset_image(std::span<const std::uint8_t> bytes)
{
	if (!compiled_image(bytes))
		return LoadImageFromMemory(".png", bytes.data(), static_cast<int>(bytes.size()));
	const auto decoded = decode_asset_image(bytes);
	Image image{MemAlloc(static_cast<unsigned int>(decoded.rgba.size())), decoded.width, decoded.height, 1,
	            PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
	if (!image.data)
		throw std::runtime_error("Image allocation failed");
	std::copy(decoded.rgba.begin(), decoded.rgba.end(), static_cast<std::uint8_t *>(image.data));
	return image;
}
namespace
{
std::map<std::string, std::weak_ptr<Texture2D>> shared_textures;
std::map<std::string, Bytes> bundled_images;
} // namespace
TextureCache::~TextureCache()
{
	clear();
}
void TextureCache::clear()
{
	textures_.clear();
	std::erase_if(shared_textures,
	              [](const auto &entry)
	              {
		              return entry.second.expired();
	              });
}
Texture2D TextureCache::load(const std::string &id, std::span<const std::uint8_t> bytes)
{
	if (const auto found = textures_.find(id); found != textures_.end())
		return *found->second;
	const auto hash = sha256(bytes);
	auto texture = shared_textures[hash].lock();
	if (!texture)
	{
		const auto image = load_asset_image(bytes);
		if (!image.data)
			throw std::runtime_error(id + ": image cannot decode");
		int maximum = 0;
#ifdef __EMSCRIPTEN__
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
#else
		using GetInteger = void (*)(unsigned, int *);
		if (const auto get = reinterpret_cast<GetInteger>(rlGetProcAddress("glGetIntegerv")))
			get(0x0D33, &maximum);
#endif
		if (maximum > 0 && (image.width > maximum || image.height > maximum))
		{
			UnloadImage(image);
			throw std::runtime_error(id + ": image exceeds this graphics device's texture limit");
		}
		const auto uploaded = LoadTextureFromImage(image);
		UnloadImage(image);
		if (!uploaded.id)
			throw std::runtime_error(id + ": texture upload failed");
		texture = std::shared_ptr<Texture2D>(new Texture2D(uploaded),
		                                     [](Texture2D *value)
		                                     {
			                                     UnloadTexture(*value);
			                                     delete value;
		                                     });
		shared_textures[hash] = texture;
	}
	textures_[id] = texture;
	return *texture;
}
Texture2D TextureCache::load_file(const std::string &path)
{
	if (!bundled_images.contains(path))
		bundled_images[path] = read_file(path);
	return load(path, bundled_images.at(path));
}
struct GameScreen::State
{
	FileImportInbox imports;
	std::string mission, slot, ship, seat, notice;
	ConsoleModel console_model;
	TextureCache textures;
	std::map<std::string, std::unique_ptr<RadarSweep>> sweeps;
	SceneRenderer scene;
	Texture2D invitation{};
	Texture2D viewscreen_frame{};
	std::string invitation_url;
	bool show_invitation = false;
	bool environment_ready = false;
	std::optional<BodyView> shake_previous;
	std::uint64_t shake_tick = 0;
	Vector2 shake_acceleration{};
	float shake_energy = 0;
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
	std::optional<Observation> observation;
	bool was_running = false;
	std::string previous_markup, previous_description, previous_network;
	bool markup_ready = false;
	bool preload_planned = false, preload_finished = false;
	std::vector<std::string> preload_images, preload_definitions, preload_ui;
	std::size_t preload_done = 0;
	std::string preload_error;
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
		environment_ready = false;
		shake_previous.reset();
		shake_acceleration = {};
		shake_energy = 0;
		environment = {};
		presentation_identity = nullptr;
		grid_definition.clear();
		markup_ready = false;
		preload_planned = preload_finished = false;
		preload_ui.clear();
		preload_images.clear();
		preload_definitions.clear();
		preload_done = 0;
		preload_error.clear();
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
				description = "Tick " + std::to_string(app.session->ticks());
				if (auto own = focus_body)
					description += " / position " + std::to_string(own->x / 1000) + ", " +
					               std::to_string(own->y / 1000) + " m / speed " +
					               std::to_string(static_cast<int>(
					                   std::hypot(static_cast<double>(own->vx), static_cast<double>(own->vy)) / 1000)) +
					               " m/s / heading " + std::to_string(own->heading / 1000) + " degrees.";
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
			environment.own = app.session->focus_body();
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
								camera_aspect =
								    std::max(camera_aspect, static_cast<float>(frame.rect[2]) / frame.rect[3]);
						};
						frames(sprite.frames);
						for (const auto &entry : sprite.entries)
							frames(entry.frames);
					}
					else
						for (const auto &reference : render.at("images"))
						{
							const auto &png = app.session->catalog().assets.at(reference.get<std::string>());
							auto image = LoadImageFromMemory(".png", png.data(), static_cast<int>(png.size()));
							if (image.height > 0)
								camera_aspect = std::max(camera_aspect, static_cast<float>(image.width) / image.height);
							UnloadImage(image);
						}
					if (render.contains("reference_grid"))
						environment.grid = compile_reference_grid(render.at("reference_grid"), "render.reference_grid");
				}
				presentation_identity = presentation;
				grid_definition = definition;
				environment_ready = true;
			}
			if (!environment.own)
				return;
			const auto &own = *environment.own;
			const auto settings = ship_camera_state(app.session->public_board());
			const auto scale = app.session->catalog().entities.at(own.definition).at("render").at("scale").get<float>();
			const auto pose =
			    ship_camera_pose(own.x, own.y, own.heading, scale, camera_aspect, settings.mode, settings.direction);
			const float heading = static_cast<float>(pose.bearing);
			const float ahead = std::max(2.f, scale * .2f) * 8.f;
			Camera3D camera{{static_cast<float>(pose.position[0]), static_cast<float>(pose.position[1]),
			                 static_cast<float>(pose.position[2])},
			                {static_cast<float>(pose.target[0]), static_cast<float>(pose.target[1]),
			                 static_cast<float>(pose.target[2])},
			                {0, 1, 0},
			                50,
			                CAMERA_PERSPECTIVE};
			const auto tick = app.session->ticks();
			if (!shake_previous || shake_previous->id != own.id || tick < shake_tick)
			{
				shake_previous = own;
				shake_tick = tick;
				shake_acceleration = {};
				shake_energy = 0;
			}
			else if (tick > shake_tick)
			{
				const float seconds = static_cast<float>(tick - shake_tick) / 20.f;
				const Vector2 acceleration{(own.vx - shake_previous->vx) / (1000.f * seconds),
				                           (own.vy - shake_previous->vy) / (1000.f * seconds)};
				const float change =
				    std::hypot(acceleration.x - shake_acceleration.x, acceleration.y - shake_acceleration.y);
				shake_energy = std::min(1.f, shake_energy + change * .025f);
				shake_previous = own;
				shake_tick = tick;
				shake_acceleration = acceleration;
			}
			shake_energy *= std::exp(-8.f * GetFrameTime());
			if (app.reduced_motion)
				shake_energy = 0;
			const float shake = shake_energy * ahead * .006f;
			camera.target.x += std::sin(heading) * shake * std::sin(GetTime() * 71);
			camera.target.z += std::cos(heading) * shake * std::sin(GetTime() * 71);
			camera.target.y += shake * std::sin(GetTime() * 53);
			const auto stage = GetTime();
			auto bodies = app.session->scene();
			std::erase_if(bodies,
			              [&](const auto &body)
			              {
				              return settings.mode == 0 && body.id == own.id;
			              });
			scene.draw(app.session->catalog(), bodies, camera, area(), app.session->scene_lighting(), GetTime(),
			           SceneDiagnostic::comic, {}, environment);
			scene_metrics["scene_render"] = GetTime() - stage;
			const auto shared = app.session->public_board().value("display", Json(nullptr));
			if (shared.is_object())
			{
				const auto rect = area();
				DrawRectangleRec(rect, {0, 0, 0, 166});
				const Rectangle plot{rect.x + rect.width * .04f, rect.y + rect.height * .08f, rect.width * .92f,
				                     rect.height * .84f};
				const auto own = display::read_body(shared.at("own"));
				if (shared.at("kind") == "navigation")
				{
					NavigationCamera camera;
					camera.x = shared.at("camera")[0];
					camera.y = shared.at("camera")[1];
					camera.scale = shared.at("camera")[2].get<double>() * 100 / plot.width;
					draw_navigation_plot(own, shared.at("regions"), shared.at("objectives"), shared.at("waypoint"),
					                     camera, plot, true);
				}
				else
				{
					Observation view{own, {}};
					view.regions = shared.at("regions");
					auto sensor = display::read_radar(shared.at("radar"));
					for (const auto &contact : shared.at("contacts"))
					{
						view.contacts.push_back(display::read_body(contact));
						sensor.contact_ids.push_back(view.contacts.back().id);
					}
					auto &sweep = sweeps["shared/" + shared.at("console").get<std::string>() + "/" +
					                     shared.at("widget").get<std::string>()];
					if (!sweep)
						sweep = std::make_unique<RadarSweep>();
					draw_radar_plot(textures, app.session->catalog(), view, sensor, shared.at("waypoint"), *sweep,
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
				// Nine-slice without filling the live view: keep corner size modest on large displays.
				DrawTextureNPatch(
				    viewscreen_frame,
				    {{0, 0, static_cast<float>(viewscreen_frame.width), static_cast<float>(viewscreen_frame.height)},
				     112,
				     112,
				     112,
				     112,
				     NPATCH_NINE_PATCH},
				    rect, {0, 0}, 0, WHITE);
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
	if (!app.session)
		return {};
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
		// Yield to the platform after each bounded resource; GPU work stays on the graphics thread.
		if (s.preload_done < s.preload_images.size())
		{
			const auto &id = s.preload_images.at(s.preload_done);
			const auto &bytes = catalog.assets.at(id);
			if (compiled_image(bytes) ||
			    (bytes.size() >= 4 && bytes[0] == 137 && bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71))
				s.textures.load(id, bytes);
			++s.preload_done;
		}
		else if (s.preload_done < s.preload_images.size() + s.preload_definitions.size())
		{
			s.scene.preload(catalog, s.preload_definitions.at(s.preload_done - s.preload_images.size()));
			++s.preload_done;
		}
		else if (s.preload_done < total - 1)
		{
			s.textures.load_file(
			    s.preload_ui.at(s.preload_done - s.preload_images.size() - s.preload_definitions.size()));
			++s.preload_done;
		}
		else if (s.preload_done == total - 1)
		{
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
		return {true, s.preload_done, total, "Preparing graphics", title, {}};
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
void GameScreen::prepare(Application &app, Rml::ElementDocument &document)
{
	state_->prepare(app, document);
}
void GameScreen::draw(const Application &app)
{
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
	state_->display_region = region;
	state_->draw(app);
	for (const auto &[name, duration] : state_->scene_metrics)
		app.frame_metrics[name] = duration;
	state_->display_region.reset();
}
} // namespace phoenix
