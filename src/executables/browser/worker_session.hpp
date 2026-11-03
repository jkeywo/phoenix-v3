#pragma once

#include "host_projection.hpp"

// The ordinary browser page reads the most recently delivered projection and
// sends intents asynchronously. It never loads the authoritative mission pack.
class BrowserWorkerSession final : public phoenix::SessionPort
{
	using Json = phoenix::Json;
	using Bytes = phoenix::Bytes;
	phoenix::Catalog catalog_, incoming_catalog_;
	phoenix::LobbyProjection lobby_;
	std::map<std::uint64_t, std::shared_ptr<const phoenix::Observation>> observations_;
	std::shared_ptr<const std::vector<phoenix::SceneBody>> scene_ =
	    std::make_shared<const std::vector<phoenix::SceneBody>>();
	std::vector<phoenix::BodyView> viewscreen_;
	std::vector<phoenix::ConsoleSeat> seats_;
	std::vector<std::pair<std::string, std::string>> controls_;
	std::optional<phoenix::BodyView> focus_;
	Json lighting_, presentation_, board_, inspection_;
	std::string phase_ = "Opening host lobby", loading_, error_;
	std::uint64_t generation_ = 0, ticks_ = 0;
	bool running_ = false, catalog_ready_ = false, attaching_ = false;

	static bool send(const Json &message)
	{
		const auto text = message.dump();
		// clang-format off
		return EM_ASM_INT({ return Module.phoenixHostRequest?.(JSON.parse(UTF8ToString($0))) ? 1 : 0; },
		                  text.c_str()) != 0;
		// clang-format on
	}
	[[noreturn]] static void unsupported()
	{
		throw std::runtime_error("This operation requires a browser host request");
	}

public:
	explicit BrowserWorkerSession(const Json &menu) : catalog_(phoenix::menu_catalog({menu}))
	{
		// The menu already has a content identity while the mission archive is
		// still absent from this process.
		catalog_.ordered_packs.emplace_back(menu.at("id").get<std::string>(), menu.at("hash").get<std::string>());
	}
	void receive(const Json &data)
	{
		lobby_ = data.at("lobby").get<phoenix::LobbyProjection>();
		phase_ = data.at("phase");
		if (phase_ != "Loading mission")
			attaching_ = false;
		loading_ = data.at("loading");
		running_ = catalog_ready_ && data.at("running").get<bool>();
		ticks_ = data.at("ticks");
		generation_ = data.at("generation");
		seats_ = data.at("seats_available").get<std::vector<phoenix::ConsoleSeat>>();
		controls_ = data.at("controls").get<decltype(controls_)>();
		inspection_ = data.at("inspection");
		board_ = data.at("board");
		if (running_ && data.contains("scene"))
		{
			scene_ = std::make_shared<const std::vector<phoenix::SceneBody>>(
			    data.at("scene").get<std::vector<phoenix::SceneBody>>());
			viewscreen_ = data.at("viewscreen").get<std::vector<phoenix::BodyView>>();
			lighting_ = data.at("lighting");
			presentation_ = data.at("presentation");
			focus_ = data.contains("focus") ? std::optional(data.at("focus").get<phoenix::BodyView>()) : std::nullopt;
			observations_.clear();
			if (const auto found = data.find("observations"); found != data.end())
				for (const auto &[route, value] : found->items())
					observations_.emplace(std::stoull(route), std::make_shared<const phoenix::Observation>(
					                                              phoenix::network::read_observation(value, catalog_)));
		}
	}
	void catalog_begin(const Json &data)
	{
		incoming_catalog_ = phoenix::read_host_render_catalog(data);
	}
	void catalog_asset(const std::string &name, const std::uint8_t *data, std::size_t size)
	{
		incoming_catalog_.assets.emplace(name, Bytes(data, data + size));
	}
	void catalog_ready()
	{
		catalog_ = std::move(incoming_catalog_);
		catalog_ready_ = true;
	}
	void attaching()
	{
		attaching_ = true;
		error_.clear();
	}
	void error(std::string message)
	{
		attaching_ = false;
		error_ = std::move(message);
	}
	const phoenix::Catalog &catalog() const override
	{
		return catalog_;
	}
	std::uint64_t generation() const override
	{
		return generation_;
	}
	std::uint64_t ticks() const override
	{
		return ticks_;
	}
	bool running() const override
	{
		return running_;
	}
	bool networked() const override
	{
		return true;
	}
	bool recovery_pending() const override
	{
		return false;
	}
	std::string loading_scenario() const override
	{
		return catalog_ready_ ? loading_ : std::string{};
	}
	std::string preparation_phase() const override
	{
		if (running_)
			return {};
		if (!error_.empty())
			return error_;
		return attaching_ ? "Preparing session" : phase_;
	}
	std::string connection_status() const override
	{
		return error_.empty() ? lobby_.status : error_;
	}
	std::string invitation_code() const override
	{
		return lobby_.code;
	}
	phoenix::LobbyProjection lobby() const override
	{
		auto result = lobby_;
		if (!error_.empty())
			result.status = error_;
		return result;
	}
	std::vector<phoenix::ConsoleSeat> console_seats() const override
	{
		return seats_;
	}
	decltype(controls_) lobby_controls() const override
	{
		auto result = controls_;
		if (!error_.empty() && phase_ == "Loading mission")
			result.emplace_back("lobby.retry-preparation", "Retry preparation");
		return result;
	}
	std::optional<phoenix::BodyView> focus_body() const override
	{
		return focus_;
	}
	std::vector<phoenix::BodyView> viewscreen() const override
	{
		return viewscreen_;
	}
	std::vector<phoenix::SceneBody> scene() const override
	{
		return *scene_;
	}
	std::shared_ptr<const std::vector<phoenix::SceneBody>> scene_snapshot() const override
	{
		return scene_;
	}
	Json scene_lighting() const override
	{
		return lighting_;
	}
	Json scene_presentation() const override
	{
		return presentation_;
	}
	Json public_board() const override
	{
		return board_;
	}
	Json inspection() const override
	{
		return inspection_;
	}
	std::shared_ptr<const phoenix::Observation> console_snapshot(std::uint64_t route) const override
	{
		const auto found = observations_.find(route);
		return found == observations_.end() ? nullptr : found->second;
	}
	std::optional<phoenix::Observation> console_observation(std::uint64_t route) const override
	{
		const auto view = console_snapshot(route);
		return view ? std::optional(*view) : std::nullopt;
	}
	std::optional<phoenix::Observation> observation() const override
	{
		return console_observation(1);
	}
	bool session_action(const std::string &id) override
	{
		if (id == "session.export-save" || id == "session.store-save")
			return send({{"type", "save"}, {"store", id == "session.store-save"}});
		if (id == "lobby.retry-preparation" && !error_.empty() && phase_ == "Loading mission")
		{
			if (!send({{"type", "retry-mission"}}))
				return false;
			error_.clear();
			return true;
		}
		return send({{"type", "action"}, {"action", id}});
	}
	bool lobby_command(const phoenix::LobbyCommand &value) override
	{
		return command_request({{"op", "lobby"}, {"value", value}});
	}
	bool set_player_profile(const std::string &name, const Bytes &avatar) override
	{
		return command_request({{"op", "profile"}, {"name", name}, {"avatar", avatar}});
	}
	bool set_local_player_profile(std::uint64_t route, const std::string &name, const Bytes &avatar) override
	{
		return command_request({{"op", "local-profile"}, {"route", route}, {"name", name}, {"avatar", avatar}});
	}
	bool claim_console(std::uint64_t route, const phoenix::ConsoleAddress &address) override
	{
		return command_request({{"op", "claim"}, {"route", route}, {"address", address}});
	}
	bool release_console(std::uint64_t route) override
	{
		return command_request({{"op", "release"}, {"route", route}});
	}
	bool dispatch_console_command(std::uint64_t route, const phoenix::OperatingCommand &value) override
	{
		return command_request(
		    {{"op", "command"}, {"route", route}, {"value", phoenix::encode_operating_command(value)}});
	}
	bool console_command(std::uint64_t route, const phoenix::OperatingCommand &value) override
	{
		return dispatch_console_command(route, value);
	}
	bool command(const phoenix::OperatingCommand &value) override
	{
		return dispatch_console_command(1, value);
	}
	void tick() override
	{
	}
	void add_pack(const Bytes &) override
	{
		unsupported();
	}
	void clear_packs() override
	{
		send({{"type", "close"}});
	}
	void start(const phoenix::GameSetup &) override
	{
		unsupported();
	}
	Bytes save() const override
	{
		unsupported();
	}
	void restore(const Bytes &) override
	{
		unsupported();
	}
	void resume_local() override
	{
		session_action("lobby.ready");
	}

private:
	static bool command_request(const Json &value)
	{
		return send({{"type", "command"}, {"command", value}});
	}
};
