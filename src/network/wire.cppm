module;
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.wire;
export import phoenix.content;

export namespace phoenix
{
struct GamePacket
{
	std::string session, from, to, type;
	Json body;
};
std::string random_token();
std::string encode_packet(const GamePacket &packet, const std::string &key);
std::optional<GamePacket> decode_packet(const std::string &bytes, const std::string &sender,
                                        const std::string &recipient, const std::string &session,
                                        const std::string &key);
class PackSource
{
public:
	explicit PackSource(std::vector<Bytes> archives);
	Json offer() const;
	Json chunk(const Json &request) const;

private:
	std::vector<Bytes> archives_;
	Json offer_;
};
class PackTransfer
{
public:
	// Cache entries are rehashed/reloaded; cached filenames never establish identity.
	explicit PackTransfer(const Json &offer, const std::map<std::string, Bytes> &cache = {});
	std::vector<Json> requests(std::uint64_t now_ms, unsigned window = 4);
	bool accept(const Json &chunk);
	bool ready() const;
	std::size_t received_bytes() const;
	std::size_t total_bytes() const;
	const Catalog &catalog() const;
	std::vector<Bytes> archives() const;

private:
	struct Pending
	{
		std::string id, hash;
		Bytes bytes;
		std::vector<bool> received;
		std::vector<std::uint64_t> deadlines;
		bool complete = false;
	};
	std::vector<Pending> packs_;
	Catalog catalog_;
	std::size_t received_ = 0, total_ = 0;
	bool ready_ = false;
	void resolve();
};
} // namespace phoenix
