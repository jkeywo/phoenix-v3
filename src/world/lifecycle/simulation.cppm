module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.simulation;
export import phoenix.content.catalog;
export import phoenix.contracts;
export import phoenix.control.controllers;

export namespace phoenix
{
class World
{
public:
	static constexpr int ticks_per_second = 20;
	World();
	~World();
	void tick();
	unsigned long long ticks() const;
	void load(const Catalog &content, const std::string &map, const std::map<std::string, std::string> &ships = {});
	// Setup-only configuration; gameplay changes use authenticated Console commands.
	bool prepare_console_profile(const ConsoleAddress &, const std::string &profile);
	bool claim(std::uint64_t console, EntityId ship, const std::string &seat = {}, bool sidebar = false);
	bool can_claim(std::uint64_t console, EntityId ship, const std::string &seat = {}, bool sidebar = false) const;
	std::vector<ConsoleSpec> consoles(EntityId ship) const;
	std::vector<EntityId> operational_fleet(EntityId origin) const;
	std::optional<EntityId> ship_for_slot(const std::string &slot) const;
	bool release(std::uint64_t console);
	// Recovery discards transport ownership without changing saved operations.
	void clear_claims();
	bool submit(std::uint64_t console, const OperatingCommand &command);
	bool can_submit(std::uint64_t console, const OperatingCommand &command) const;
	std::optional<Observation> observe(std::uint64_t console) const;
	// Immutable permitted presentation snapshot, reused until the next mutation.
	std::shared_ptr<const Observation> observe_shared(std::uint64_t console) const;
	Json controller_ai(EntityId entity) const;
	Json controller_decision_snapshot(const ControllerAddress &);
	bool configure_controller_provider(const ControllerAddress &, const std::string &provider);
	bool submit_controller_decision(const Json &decision);
	Json public_board(EntityId entity) const;
	std::vector<BodyView> inspect() const;
	std::vector<SceneBody> scene() const;
	Json scene_environment() const;
	Json save() const;
	Json debug() const;
	void restore(const Catalog &content, const Json &saved);

private:
	void load_impl(const Catalog &, const std::string &, const std::map<std::string, std::string> &, bool restoring);
	Observation observe_at(std::uint64_t claimant, const ConsoleAddress &) const;
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
