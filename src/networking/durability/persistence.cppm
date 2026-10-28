module;
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

export module phoenix.persistence;
export import phoenix.replication;
export import phoenix.platform.storage;

export namespace phoenix
{
enum class JournalRecovery
{
	allow,
	require,
	fresh
};
// Owns the asynchronous journal/consensus durability handshake. Call poll before
// processing inputs, then flush after advancing the actor. Scheduling and
// committed-entry application remain with the host or fleet policy.
class DurableConsensus
{
public:
	DurableConsensus(PeerRoster roster, const std::string &location, JournalRecovery recovery);
	~DurableConsensus();
	void poll();
	void flush();
	PeerConsensus *peer() const;
	bool restored() const;
	bool durable() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
