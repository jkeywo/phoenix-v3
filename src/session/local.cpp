module;
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

module phoenix.session.local;
import phoenix.session.api;
import phoenix.simulation;

#include "inference_host.hpp"
namespace phoenix
{
namespace
{
class LocalSession final : public HostAccess
{
	std::vector<Pack> packs_;
	Catalog catalog_;
	std::unique_ptr<World> world_;
	GameSetup setup_;
	InferenceHost inference_;
	bool active_ = false;
	std::uint64_t generation_ = 0;
	static constexpr std::uint64_t local_console = 1;

public:
	void set_inference_connection(std::shared_ptr<InferenceConnection> value) override
	{
		inference_.connection = std::move(value);
		inference_.reset();
	}
	Json inference_state() const override
	{
		Json state = inference_.connection ? inference_.connection->status() : Json::object();
		if (world_)
			if (const auto ship = world_->ship_for_slot(setup_.local_slot))
				state["controllers"] = world_->controller_ai(*ship);
		return state;
	}
	bool inference_command(const Json &command) override
	{
		if (command.value("operation", std::string{}) == "pair")
		{
			set_inference_connection(std::make_shared<InferenceConnection>(random_token(), default_network_service));
			return true;
		}

		if (!world_)
			return false;
		const auto ship = world_->ship_for_slot(setup_.local_slot);
		if (!ship)
			return false;
		return world_->configure_controller_provider({*ship, command.at("console"), command.at("controller")},
		                                             command.at("provider"));
	}
	void poll() override
	{
		if (world_ && active_)
			if (const auto ship = world_->ship_for_slot(setup_.local_slot))
				inference_.poll(*world_, *ship,
				                [&](const Json &frame)
				                {
					                return world_->submit_controller_decision(frame.at("decision"));
				                });
	}
	std::uint64_t generation() const override
	{
		return generation_;
	}
	World &authoritative_world() override
	{
		if (!world_)
			throw std::runtime_error("No authoritative world is loaded");
		return *world_;
	}
	const World &authoritative_world() const override
	{
		if (!world_)
			throw std::runtime_error("No authoritative world is loaded");
		return *world_;
	}
	const std::vector<Pack> &content_packs() const override
	{
		return packs_;
	}
	const GameSetup &setup() const override
	{
		return setup_;
	}
	void add_pack(const Bytes &archive) override
	{
		add_verified_pack(read_pack(archive));
	}
	void add_verified_pack(const Pack &pack) override
	{
		if (world_)
			throw std::runtime_error("End the running session before changing packs");
		if (packs_.size() >= 32)
			throw std::runtime_error("At most 32 ordered packs are supported");
		auto candidate = packs_;
		candidate.push_back(pack);
		if (candidate.back().manifest().value("format", std::string{}) == "phoenix-runtime-save")
			throw std::runtime_error("Use Load runtime save for this ZIP");
		auto resolved = resolve_packs(candidate);
		if (resolved.presentation)
			throw std::runtime_error("Console presentation assets are not gameplay packs");
		packs_ = std::move(candidate);
		catalog_ = std::move(resolved);
	}
	void replace_packs(const std::vector<Bytes> &archives) override
	{
		if (world_)
			throw std::runtime_error("End the session before changing packs");
		auto candidate = std::make_shared<LocalSession>();
		for (const auto &archive : archives)
			candidate->add_pack(archive);
		packs_ = std::move(candidate->packs_);
		catalog_ = std::move(candidate->catalog_);
		++generation_;
	}
	void clear_packs() override
	{
		world_.reset();
		packs_.clear();
		catalog_ = {};
		setup_ = {};
		active_ = false;
		++generation_;
	}
	std::string loading_scenario() const override
	{
		return setup_.mission;
	}
	const Catalog &catalog() const override
	{
		return catalog_;
	}
	void start(const GameSetup &setup) override
	{
		start_world(setup, true);
	}
	void start_server(const GameSetup &setup) override
	{
		start_world(setup, false);
	}
	void start_world(const GameSetup &setup, bool claim_local)
	{
		if (!catalog_.maps.contains(setup.mission))
			throw std::runtime_error("Select a loaded mission");
		if (setup.gm_host && (claim_local || !setup.local_slot.empty() || !setup.local_console.empty()))
			throw std::runtime_error("A GM host cannot claim a player ship or console");
		if (!setup.gm_host && !setup.ships.contains(setup.local_slot))
			throw std::runtime_error("Select a ship for the local slot");
		if (!setup.gm_host)
		{
			const auto &ship = catalog_.entities.at(setup.ships.at(setup.local_slot));
			if (!ship.contains("console") && !ship.contains("stations"))
				throw std::runtime_error("The local ship needs an authored console");
		}
		auto candidate = std::make_unique<World>();
		candidate->load(catalog_, setup.mission, setup.ships);
		for (const auto &[slot, profiles] : setup.console_profiles)
		{
			const auto target = candidate->ship_for_slot(slot);
			if (!target || !setup.ships.contains(slot))
				throw std::runtime_error("Console profile names an unselected slot");
			for (const auto &[console, profile] : profiles)
				if (!candidate->prepare_console_profile({*target, console}, profile))
					throw std::runtime_error("Invalid initial Console complexity: " + slot + "/" + console + "/" +
					                         profile);
		}
		const auto controlled = candidate->ship_for_slot(setup.local_slot);
		auto selected_console = setup.local_console;
		if (!claim_local && selected_console.empty() && controlled)
		{
			const auto seats = candidate->consoles(*controlled);
			const auto primary = std::find_if(seats.begin(), seats.end(),
			                                  [](const auto &seat)
			                                  {
				                                  return !seat.human_seeking;
			                                  });
			if (primary != seats.end())
				selected_console = primary->id;
		}
		if (!setup.gm_host && (!controlled || !candidate->can_claim(local_console, *controlled, selected_console)))
			throw std::runtime_error("The selected ship cannot admit a local console");
		if (claim_local)
			candidate->claim(local_console, *controlled, setup.local_console);
		setup_ = setup;
		setup_.local_console = selected_console;
		inference_.reset();
		world_ = std::move(candidate);
		active_ = true;
		++generation_;
	}
	bool running() const override
	{
		return world_ && active_;
	}
	bool recovery_pending() const override
	{
		return world_ && !active_;
	}
	Bytes save() const override
	{
		return write_pack(save_container(), PackEncoding::Zip, ArchivePurpose::Save);
	}
	PackDraft save_container() const override
	{
		if (!running())
			throw std::runtime_error("Only a running local session can be saved");
		auto state = world_->save();
		PackDraft container;
		container.manifest = {{"schema", 1}, {"id", "phoenix-session-save"}, {"format", "phoenix-runtime-save"}};
		container.files["definitions.json"] = json_bytes(Json{{"schema", 1}});
		Json paths = Json::array();
		constexpr std::size_t chunk_size = 8 * 1024 * 1024;
		for (std::size_t i = 0; i < packs_.size(); ++i)
		{
			Json chunks = Json::array();
			const auto &bytes = packs_[i].archive();
			for (std::size_t offset = 0; offset < bytes.size(); offset += chunk_size)
			{
				auto path = "packs/" + std::to_string(i) + "/" + std::to_string(offset / chunk_size) + ".bin";
				chunks.push_back(path);
				auto end = std::min(bytes.size(), offset + chunk_size);
				container.files[path] = Bytes(bytes.begin() + offset, bytes.begin() + end);
			}
			paths.push_back(chunks);
		}
		auto data = Json{{"schema", state.at("schema")},
		                 {"mission", setup_.mission},
		                 {"ships", setup_.ships},
		                 {"local_slot", setup_.local_slot},
		                 {"gm_host", setup_.gm_host},
		                 {"packs", paths},
		                 {"state", state}};
		if (state.at("schema").get<int>() >= 2)
			data["local_console"] = setup_.local_console;
		container.files["session.json"] = json_bytes(data);
		return container;
	}
	void restore(const Bytes &archive) override
	{
		restore_container(read_pack(archive, ArchivePurpose::Save).draft());
	}
	void restore_container(const PackDraft &container) override
	{
		if (container.manifest.value("format", std::string{}) != "phoenix-runtime-save")
			throw std::runtime_error("Select a Phoenix runtime save ZIP");
		const auto data = parse_json(container.files.at("session.json"), "session.json");
		if (data.at("schema") != 23)
			throw std::runtime_error("Saved state schema retired; start a new session (schema 23 required)");
		if (!data.at("packs").is_array() || data.at("packs").empty() || data.at("packs").size() > 32)
			throw std::runtime_error("Unsupported runtime save content roster");
		LocalSession candidate;
		for (const auto &chunks : data.at("packs"))
		{
			if (!chunks.is_array() || chunks.empty() || chunks.size() > 4)
				throw std::runtime_error("Invalid saved pack chunk list");
			Bytes bytes;
			for (const auto &path : chunks)
			{
				const auto &chunk = container.files.at(path.get<std::string>());
				if (chunk.size() > 8 * 1024 * 1024 || bytes.size() + chunk.size() > 32 * 1024 * 1024)
					throw std::runtime_error("Saved pack exceeds archive limits");
				bytes.insert(bytes.end(), chunk.begin(), chunk.end());
			}
			candidate.packs_.push_back(read_pack(std::move(bytes)));
		}
		candidate.catalog_ = resolve_packs(candidate.packs_);
		if (!catalog_.base.id.empty() && candidate.catalog_.base != catalog_.base)
			throw std::runtime_error("Relaunch with this save's base pack revision");
		GameSetup setup{data.at("mission"), data.at("ships").get<std::map<std::string, std::string>>(),
		                data.at("local_slot"), data.value("local_console", std::string{})};
		setup.gm_host = data.value("gm_host", false);
		// Verify that recovery can admit the selected console before touching live state.
		candidate.start_server(setup);
		candidate.world_->restore(candidate.catalog_, data.at("state"));
		if (data.at("state").at("map") != setup.mission || data.at("state").at("ships") != Json(setup.ships))
			throw std::runtime_error("Saved setup and world disagree");
		// A portable save carries historical claims, never a live operator lease.
		candidate.world_->clear_claims();
		const auto controlled = candidate.world_->ship_for_slot(setup.local_slot);
		if (!setup.gm_host &&
		    (!controlled || !candidate.world_->can_claim(local_console, *controlled, candidate.setup_.local_console)))
			throw std::runtime_error("The recovered ship cannot admit a local console");
		candidate.active_ = false;
		packs_ = std::move(candidate.packs_);
		catalog_ = std::move(candidate.catalog_);
		setup_ = std::move(candidate.setup_);
		inference_.reset();
		world_ = std::move(candidate.world_);
		active_ = false;
		++generation_;
	}
	void resume_local() override
	{
		if (!recovery_pending())
			throw std::runtime_error("There is no paused recovery session");
		auto ship = world_->ship_for_slot(setup_.local_slot);
		if (!setup_.gm_host && (!ship || !world_->claim(local_console, *ship, setup_.local_console)))
			throw std::runtime_error("Recovery console claim was refused");
		active_ = true;
	}
	void resume_server() override
	{
		if (!recovery_pending())
			throw std::runtime_error("There is no paused recovery session");
		active_ = true;
	}
	void tick() override
	{
		if (running())
			world_->tick();
	}
	std::uint64_t ticks() const override
	{
		return world_ ? world_->ticks() : 0;
	}
	bool command(const OperatingCommand &command) override
	{
		return running() && world_->submit(local_console, command);
	}
	std::optional<Observation> observation() const override
	{
		return running() ? world_->observe(local_console) : std::nullopt;
	}
	std::vector<BodyView> viewscreen() const override
	{
		return running() ? world_->inspect() : std::vector<BodyView>{};
	}
	std::optional<BodyView> focus_body() const override
	{
		if (!running())
			return {};
		const auto ship = world_->ship_for_slot(setup_.local_slot);
		for (const auto &body : world_->inspect())
			if (ship == body.id)
				return body;
		return {};
	}
	std::vector<SceneBody> scene() const override
	{
		return running() ? world_->scene() : std::vector<SceneBody>{};
	}
	Json scene_lighting() const override
	{
		if (!running())
			return Json::object();
		auto result = catalog_.maps.at(setup_.mission).value("lighting", Json::object());
		result["environment"] = world_->scene_environment();
		return result;
	}
	Json scene_presentation() const override
	{
		return running() ? catalog_.maps.at(setup_.mission).value("presentation", Json::object()) : Json::object();
	}
	Json public_board() const override
	{
		const auto focus = focus_body();
		return focus ? world_->public_board(focus->id) : Json::object();
	}
	Json inspection() const override
	{
		return running() ? world_->save() : Json(nullptr);
	}
	std::vector<ConsoleSeat> console_seats() const override
	{
		std::vector<ConsoleSeat> result;
		if (!running())
			return result;
		for (const auto &[slot, definition] : setup_.ships)
			if (auto ship = world_->ship_for_slot(slot))
				for (auto member : world_->operational_fleet(*ship))
					for (const auto &seat : world_->consoles(member))
						result.push_back({{member, seat.id}, seat, world_->can_claim(UINT64_MAX, member, seat.id)});
		return result;
	}
	bool claim_console(std::uint64_t route, const ConsoleAddress &seat) override
	{
		return running() && world_->claim(route, seat.ship, seat.console, route == 31);
	}
	bool release_console(std::uint64_t route) override
	{
		return running() && world_->release(route);
	}
	bool console_command(std::uint64_t route, const OperatingCommand &value) override
	{
		return running() && world_->submit(route, value);
	}
	std::optional<Observation> console_observation(std::uint64_t route) const override
	{
		return running() ? world_->observe(route) : std::nullopt;
	}
};
} // namespace
std::shared_ptr<HostAccess> make_host_world()
{
	return std::make_shared<LocalSession>();
}
std::shared_ptr<SessionPort> make_local_session()
{
	return make_host_world();
}
} // namespace phoenix
