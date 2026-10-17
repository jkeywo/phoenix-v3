#pragma once
#include <phoenix/consensus.hpp>
namespace phoenix {
struct PeerStoreResult {
    bool opened = false;
    std::uint64_t generation = 0;
    WireJson restored;
    std::string error;
};
class PeerStore {
public:
    virtual ~PeerStore() = default;
    // Accept at most one outstanding write; false means backpressure or failed store.
    virtual bool write(const DurableWrite& transaction) = 0;
    virtual std::optional<PeerStoreResult> poll() = 0;
};
// Native: journal file path. Browser: origin-local IndexedDB namespace.
std::unique_ptr<PeerStore> open_peer_store(const std::string& location);
}
