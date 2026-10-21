module;
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

export module phoenix.session.execution;
export import phoenix.session.local;

export namespace phoenix
{
struct ConsoleLease
{
	ConsoleAddress address;
	std::string server, owner, token;
	std::uint64_t route = 1, claimant = 0, sequence = 0;
	bool active = false;
};
// Shared committed-frame executor for singleton and fixed-roster hosts.
class GameExecution
{
public:
	GameExecution(std::shared_ptr<HostAccess> world, std::map<std::string, std::string> slots);
	std::vector<ConsoleSeat> seats(const std::string &server) const;
	const ConsoleLease *lease(const ConsoleAddress &) const;
	const ConsoleLease *owned(const std::string &server, const std::string &owner, std::uint64_t route = 1) const;
	bool can_claim(const std::string &server, const std::string &owner, std::uint64_t route, const ConsoleAddress &,
	               bool replace) const;
	bool can_command(const std::string &server, const std::string &owner, std::uint64_t route, const std::string &token,
	                 std::uint64_t sequence, const OperatingCommand &) const;
	bool apply(const std::string &server, const Json &frame);
	void absent(const std::string &server);
	std::optional<Observation> observe(const std::string &server, const std::string &owner,
	                                   std::uint64_t route = 1) const;
	Json snapshot() const;
	bool checkpoint_ready() const
	{
		return checkpoint_ready_;
	}
	void restore(const Json &state);

private:
	void refresh_leases();
	std::shared_ptr<HostAccess> world_;
	std::map<std::string, std::string> slots_;
	std::vector<ConsoleLease> leases_;
	std::uint64_t next_claimant_ = 1;
	bool checkpoint_ready_ = true;
};
} // namespace phoenix
