module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

export module phoenix.control.ownership;
export import phoenix.contracts;

export namespace phoenix
{
// Compiled participation metadata. No physical state or player/UI identity.
struct ConsoleParticipation
{
	std::string id, station;
	bool human_seeking = false, viewscreen = false;
	std::vector<std::string> human_preferences;
};
struct ShipParticipation
{
	std::vector<ConsoleParticipation> consoles;
	std::string sidebar_default;
};

// Owns human claims and Away participation policy. The World separately checks
// physical availability and Controller command eligibility before admission.
class ConsoleOwnership
{
public:
	void configure_ship(EntityId ship, ShipParticipation participation);
	bool can_claim(std::uint64_t claimant, const ConsoleAddress &, bool sidebar = false) const;
	bool claim(std::uint64_t claimant, const ConsoleAddress &, bool sidebar = false);
	std::optional<ConsoleAddress> release(std::uint64_t claimant);
	std::vector<std::uint64_t> release_ship(EntityId ship);
	void clear_claims();
	bool empty() const;
	std::optional<ConsoleAddress> claim_for(std::uint64_t claimant) const;
	std::uint64_t operator_for(const ConsoleAddress &) const;
	bool owns_command(std::uint64_t claimant, const ConsoleAddress &) const;
	bool is_away(const ConsoleAddress &) const;
	void set_away(const ConsoleAddress &, bool);
	// Read-only views retain the established checkpoint encoding and stable order.
	const std::map<std::uint64_t, ConsoleAddress> &claims() const;
	const std::map<ConsoleAddress, bool> &away() const;

private:
	std::map<EntityId, ShipParticipation> participation_;
	std::map<std::uint64_t, ConsoleAddress> claims_;
	std::map<ConsoleAddress, bool> away_;
	const ConsoleParticipation *console(const ConsoleAddress &) const;
};
} // namespace phoenix
