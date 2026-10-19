module;
#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.wire;
export import phoenix.content;

// Module-internal assembly mechanics; envelope and domain validation stay in the wrappers.
namespace phoenix::wire_detail
{
class ChunkBuffer
{
public:
	ChunkBuffer() = default;
	ChunkBuffer(std::size_t length, std::string hash);
	unsigned active(std::uint64_t now) const;
	std::vector<std::size_t> requests(std::uint64_t now, unsigned &budget, std::uint64_t retry_ms);
	bool accept(std::size_t offset, std::span<const std::uint8_t> data);
	static void duplicate(std::span<const std::uint8_t> original, std::size_t offset,
	                      std::span<const std::uint8_t> data);
	bool complete() const;
	const Bytes &bytes() const;
	Bytes release();

private:
	Bytes bytes_;
	std::string hash_;
	std::vector<bool> received_;
	std::vector<std::uint64_t> deadlines_;
	bool complete_ = false;
};
} // namespace phoenix::wire_detail

export namespace phoenix
{
struct GamePacket
{
	std::string session, from, to, type;
	Json body;
};
std::string encode_packet(const GamePacket &packet, const std::string &key);
std::optional<GamePacket> decode_packet(const std::string &bytes, const std::string &sender,
                                        const std::string &recipient, const std::string &session,
                                        const std::string &key);
class PackSource
{
public:
	explicit PackSource(std::vector<Bytes> archives);
	explicit PackSource(std::vector<Pack> packs);
	Json offer() const;
	Json chunk(const Json &request) const;

private:
	std::vector<Pack> packs_;
	Json offer_;
};
class PackTransfer
{
public:
	// Cache entries are rehashed/reloaded; cached filenames never establish identity.
	explicit PackTransfer(const Json &offer, std::map<std::string, Bytes> cache = {});
	// Validate the complete metadata before cache I/O or archive buffer allocation.
	static void validate_offer(const Json &offer);
	std::vector<Json> requests(std::uint64_t now_ms, unsigned window = 4);
	bool accept(const Json &chunk);
	bool ready() const;
	std::size_t received_bytes() const;
	std::size_t total_bytes() const;
	const Catalog &catalog() const;
	const std::vector<Pack> &packs() const;
	// Only downloaded/repaired entries need persistent cache writes.
	std::vector<Pack> uncached_packs() const;

private:
	struct Pending
	{
		std::string id, hash;
		wire_detail::ChunkBuffer buffer;
		std::optional<Pack> verified;
		std::size_t length = 0;
		bool cached = false;
	};
	std::vector<Pending> packs_;
	Catalog catalog_;
	std::vector<Pack> ordered_;
	std::size_t received_ = 0, total_ = 0;
	bool ready_ = false;
	void resolve();
};
// Bounded authenticated projections use the same chunk window/retry discipline
// as content, but only publish a complete hash-verified state to the frontend.
class StateSource
{
public:
	StateSource(const Json &state, std::uint64_t sequence);
	StateSource(Bytes archive, std::uint64_t sequence);
	Json offer() const;
	Json chunk(const Json &request) const;

private:
	Bytes bytes_;
	Json offer_;
};
class StateTransfer
{
public:
	explicit StateTransfer(const Json &offer, bool archive = false);
	std::vector<Json> requests(std::uint64_t now_ms);
	bool accept(const Json &chunk);
	bool ready() const;
	std::uint64_t sequence() const;
	const Json &state() const;
	const Bytes &archive() const;

private:
	Json offer_, state_;
	bool archive_ = false, complete_ = false;
	wire_detail::ChunkBuffer buffer_;
};
} // namespace phoenix
