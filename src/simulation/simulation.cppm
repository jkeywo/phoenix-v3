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
export import phoenix.content;
export import phoenix.contracts;

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
	bool claim(std::uint64_t console, EntityId ship, const std::string &seat = {});
	bool can_claim(std::uint64_t console, EntityId ship, const std::string &seat = {}) const;
	std::vector<ConsoleSpec> consoles(EntityId ship) const;
	std::optional<EntityId> ship_for_slot(const std::string &slot) const;
	bool release(std::uint64_t console);
	// Recovery discards transport ownership without changing saved operations.
	void clear_claims();
	bool submit(std::uint64_t console, const OperatingCommand &command);
	bool can_submit(std::uint64_t console, const OperatingCommand &command) const;
	std::optional<Observation> observe(std::uint64_t console) const;
	std::vector<BodyView> inspect() const;
	std::vector<SceneBody> scene() const;
	Json save() const;
	Json debug() const;
	void restore(const Catalog &content, const Json &saved);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
