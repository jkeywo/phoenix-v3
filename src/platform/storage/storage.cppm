module;
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

export module phoenix.platform.storage;
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
	// Accept at most one outstanding write; false means backpressure or failure.
	virtual bool write(const DurableWrite &transaction) = 0;
	virtual std::optional<PeerStoreResult> poll() = 0;
};
// Native: journal file path. Browser: origin-local IndexedDB namespace.
std::unique_ptr<PeerStore> open_peer_store(const std::string &location);
} // namespace phoenix

// Module-private contracts shared by the journal codec, worker and OS adapters.
namespace phoenix::storage
{
inline constexpr std::size_t max_journal = 128 * 1024 * 1024;
struct Recovered
{
	WireJson state;
	std::size_t complete_bytes = 0;
};
std::string encode(const WireJson &transaction);
Recovered replay(const std::string &journal, bool atomic_records);
} // namespace phoenix::storage

namespace phoenix::storage
{
// The asynchronous store owns protocol and scheduling. Each OS backend owns one
// file/ownership lock and guarantees flushed writes before returning.
class JournalFile
{
public:
	virtual ~JournalFile() = default;
	virtual std::string read() = 0;
	virtual void recover(std::size_t complete_bytes) = 0;
	virtual void close() = 0;
	virtual void reopen() = 0;
	virtual void append(const std::string &record) = 0;
};
std::unique_ptr<JournalFile> open_journal_file(const std::filesystem::path &path);
} // namespace phoenix::storage
