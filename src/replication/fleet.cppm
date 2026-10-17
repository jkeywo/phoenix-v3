module;
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

export module phoenix.fleet;
export import phoenix.replication;
export import phoenix.transport;

export namespace phoenix
{
struct FleetOptions
{
	PeerRoster roster;
	LinkOptions link;
	std::string key, fingerprint, journal;
	bool require_existing = false, activation_required = false;
};
// Authenticated fixed-roster driver. It owns no simulation and executes no game code.
// take_committed() includes membership barriers and origin-bound fleet-frame values.
class FleetCommit
{
public:
	explicit FleetCommit(FleetOptions options, std::unique_ptr<PeerTransport> transport = {});
	~FleetCommit();
	void poll();
	bool propose(const WireJson &value);
	bool propose_tick(const WireJson &value);
	std::vector<CommittedEntry> take_committed();
	bool available() const;
	bool leader() const;
	bool started() const;
	bool storage_ready() const;
	bool activate();
	bool failed() const;
	std::string status() const;
	std::uint64_t commit_index() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
