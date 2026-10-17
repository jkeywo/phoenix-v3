#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace phoenix {
using WireJson = nlohmann::json;
struct PeerRoster { std::string session, self; std::vector<std::string> members; };
enum class PeerRole { follower, candidate, leader, failed };
struct DurableWrite { std::uint64_t generation; WireJson transaction; };
// Apply an incremental durable transaction to a recovered hard-state image.
// Validation precedes mutation. The transaction cannot rewrite a committed prefix.
void apply_peer_write(WireJson& recovered, const WireJson& transaction);
struct PeerDelivery { std::string peer; WireJson packet; };
struct CommittedEntry { std::uint64_t index, term; WireJson value; };
// Single-threaded actor. Drive from the application loop, including storage completions.
// A packet's sender must be bound to the established peer identity by the transport.
class PeerConsensus {
public:
    explicit PeerConsensus(PeerRoster roster, const WireJson& restored = nullptr);
    ~PeerConsensus();
    void elapse(unsigned milliseconds);
    bool receive(const std::string& sender, const WireJson& packet);
    bool propose(const WireJson& value);
    std::optional<DurableWrite> pending_write() const;
    void persisted(std::uint64_t generation, bool success);
    std::vector<PeerDelivery> take_deliveries();
    std::vector<CommittedEntry> take_committed();
    PeerRole role() const;
    std::string leader() const;
    bool has_quorum() const;
    std::uint64_t term() const;
    std::uint64_t commit_index() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
