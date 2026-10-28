module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.gameplay.away.operations;
export import phoenix.gameplay.away.state;
export import phoenix.content.operations.types;

export namespace phoenix
{
enum class AwayLossDecision
{
	None,
	Lost,
	Pickup,
	Reopen
};
struct AwayLossFacts
{
	bool origin_assigned = false, target_destroyed = false, lost_transport = false;
	bool transport_present = false, torpedo_launched = false;
};
// Owns the mission registry and its stable ordering. World binds physical actors,
// authority, contributions and payments through explicit domain adapter methods.
class AwayOperations
{
public:
	AwayOperations();
	~AwayOperations();
	AwayOperations(const AwayOperations &);
	AwayOperations &operator=(const AwayOperations &);
	AwayOperations(AwayOperations &&) noexcept;
	AwayOperations &operator=(AwayOperations &&) noexcept;
	const std::vector<AwayDefinition> &definitions() const;
	void configure_definitions(std::vector<AwayDefinition>);
	void initialize_missions();
	void append_definition(const AwayDefinition &);
	std::span<AwayMission> missions();
	std::span<const AwayMission> missions() const;
	AwayMission &mission(std::size_t index);
	const AwayMission &mission(std::size_t index) const;
	std::uint64_t sequence() const;
	std::uint64_t next_sequence();
	void observe_sequence(std::uint64_t sequence);
	EntityId origin(const AwayDefinition &, const std::map<std::string, EntityId> &slots) const;
	bool officer_away(EntityId ship, const std::string &officer, const std::map<std::string, EntityId> &slots) const;
	bool asset_assigned(EntityId ship, const std::string &asset, const std::map<std::string, EntityId> &slots) const;
	bool craft_reserved(EntityId home, EntityId craft, const std::string &except,
	                    const std::map<std::string, EntityId> &slots) const;
	bool defender_reserved(EntityId target, const std::string &asset) const;
	bool conversation_requested(const std::string &id) const;
	std::map<std::pair<EntityId, std::string>, std::size_t>
	active_transfers(const std::map<std::string, EntityId> &slots) const;
	void mark_deployed(AwayMission &, std::optional<std::int64_t> boarding_entry);
	void finish_recovery(AwayMission &);
	std::map<std::string, std::string> personnel_dispositions(EntityId ship,
	                                                          const std::map<std::string, EntityId> &slots,
	                                                          const std::map<std::string, std::string> &postings,
	                                                          const std::set<std::string> &lost,
	                                                          std::span<const std::string> dead_officers) const;
	AwayLossDecision loss_decision(const AwayMission &, const AwayLossFacts &) const;
	Json mission_record(std::size_t index) const;
	void restore_order(const Json &rows);

private:
	std::vector<AwayDefinition> definitions_;
	std::vector<AwayMission> missions_;
	std::uint64_t sequence_ = 0;
};
} // namespace phoenix
