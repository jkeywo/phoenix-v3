module;
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

export module phoenix.persistence;
export import phoenix.replication;

export namespace phoenix
{
struct PeerStoreResult
{
	bool opened = false;
	std::uint64_t generation = 0;
	WireJson restored;
	std::string error;
};
class PeerStore
{
public:
	virtual ~PeerStore() = default;
	// Accept at most one outstanding write; false means backpressure or failed store.
	virtual bool write(const DurableWrite &transaction) = 0;
	virtual std::optional<PeerStoreResult> poll() = 0;
};
// Native: journal file path. Browser: origin-local IndexedDB namespace.
std::unique_ptr<PeerStore> open_peer_store(const std::string &location);

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
