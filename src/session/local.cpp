#include <phoenix/session.hpp>
#include "authority.hpp"
#include <phoenix/world.hpp>
#include <algorithm>
#include <stdexcept>
namespace phoenix
{
namespace
{
class LocalSession final : public HostAccess
{
	std::vector<Pack> packs_;
	std::vector<Bytes> archives_;
	Catalog catalog_;
	std::unique_ptr<World> world_;
	GameSetup setup_;
	bool active_ = false;
	static constexpr std::uint64_t local_console = 1;

public:
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
	const std::vector<Bytes> &content_archives() const override
	{
		return archives_;
	}
	const GameSetup &setup() const override
	{
		return setup_;
	}
	void add_pack(const Bytes &archive) override
	{
		if (world_)
			throw std::runtime_error("End the running session before changing packs");
		if (packs_.size() >= 32)
			throw std::runtime_error("At most 32 ordered packs are supported");
		auto candidate = packs_;
		candidate.push_back(read_pack(archive));
		if (candidate.back().manifest.value("format", std::string{}) == "phoenix-runtime-save")
			throw std::runtime_error("Use Load runtime save for this ZIP");
		auto resolved = resolve_packs(candidate);
		auto archives = archives_;
		archives.push_back(archive);
		packs_ = std::move(candidate);
		archives_ = std::move(archives);
		catalog_ = std::move(resolved);
	}
	void clear_packs() override
	{
		world_.reset();
		packs_.clear();
		archives_.clear();
		catalog_ = {};
		setup_ = {};
		active_ = false;
	}
	const Catalog &catalog() const override
	{
		return catalog_;
	}
	void start(const GameSetup &setup) override
	{
		if (!catalog_.maps.contains(setup.mission))
			throw std::runtime_error("Select a loaded mission");
		if (!setup.ships.contains(setup.local_slot))
			throw std::runtime_error("Select a ship for the local slot");
		const auto &ship = catalog_.entities.at(setup.ships.at(setup.local_slot));
		if (!ship.contains("console"))
			throw std::runtime_error("The local ship needs an authored console");
		auto candidate = std::make_unique<World>();
		candidate->load(catalog_, setup.mission, setup.ships);
		const auto controlled = candidate->ship_for_slot(setup.local_slot);
		if (!controlled || !candidate->claim(local_console, *controlled))
			throw std::runtime_error("The selected ship cannot admit a local console");
		setup_ = setup;
		world_ = std::move(candidate);
		active_ = true;
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
		if (!running())
			throw std::runtime_error("Only a running local session can be saved");
		auto state = world_->save();
		Pack container;
		container.manifest = {{"schema", 1}, {"id", "phoenix-session-save"}, {"format", "phoenix-runtime-save"}};
		container.files["definitions.json"] = json_bytes(Json{{"schema", 1}});
		Json paths = Json::array();
		constexpr std::size_t chunk_size = 8 * 1024 * 1024;
		for (std::size_t i = 0; i < archives_.size(); ++i)
		{
			Json chunks = Json::array();
			const auto &bytes = archives_[i];
			for (std::size_t offset = 0; offset < bytes.size(); offset += chunk_size)
			{
				auto path = "packs/" + std::to_string(i) + "/" + std::to_string(offset / chunk_size) + ".bin";
				chunks.push_back(path);
				auto end = std::min(bytes.size(), offset + chunk_size);
				container.files[path] = Bytes(bytes.begin() + offset, bytes.begin() + end);
			}
			paths.push_back(chunks);
		}
		container.files["session.json"] = json_bytes(Json{{"schema", 1},
		                                                  {"mission", setup_.mission},
		                                                  {"ships", setup_.ships},
		                                                  {"local_slot", setup_.local_slot},
		                                                  {"packs", paths},
		                                                  {"state", state}});
		return write_pack(container);
	}
	void restore(const Bytes &archive) override
	{
		const auto container = read_pack(archive);
		if (container.manifest.value("format", std::string{}) != "phoenix-runtime-save")
			throw std::runtime_error("Select a Phoenix runtime save ZIP");
		const auto data = parse_json(container.files.at("session.json"), "session.json");
		if (data.at("schema") != 1 || !data.at("packs").is_array() || data.at("packs").empty() ||
		    data.at("packs").size() > 32)
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
			candidate.add_pack(bytes);
		}
		GameSetup setup{data.at("mission"), data.at("ships").get<std::map<std::string, std::string>>(),
		                data.at("local_slot")};
		// Verify that recovery can admit the selected console before touching live state.
		candidate.start(setup);
		candidate.world_->restore(candidate.catalog_, data.at("state"));
		if (data.at("state").at("map") != setup.mission || data.at("state").at("ships") != Json(setup.ships))
			throw std::runtime_error("Saved setup and world disagree");
		candidate.world_->clear_claims();
		candidate.active_ = false;
		packs_ = std::move(candidate.packs_);
		archives_ = std::move(candidate.archives_);
		catalog_ = std::move(candidate.catalog_);
		setup_ = std::move(candidate.setup_);
		world_ = std::move(candidate.world_);
		active_ = false;
	}
	void resume_local() override
	{
		if (!recovery_pending())
			throw std::runtime_error("There is no paused recovery session");
		auto ship = world_->ship_for_slot(setup_.local_slot);
		if (!ship || !world_->claim(local_console, *ship))
			throw std::runtime_error("Recovery console claim was refused");
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
