module;
#include <algorithm>
#include <set>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>

module phoenix.wire;

namespace phoenix
{
namespace
{
constexpr std::size_t chunk_size = 12 * 1024, max_archive = 32 * 1024 * 1024, max_total = 64 * 1024 * 1024;
const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::size_t size(const Json &value, std::size_t maximum)
{
	if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
		throw std::runtime_error("Invalid pack transfer size/offset");
	auto number = value.get<std::uint64_t>();
	if (number > maximum)
		throw std::runtime_error("Pack transfer exceeds supported bounds");
	return static_cast<std::size_t>(number);
}
std::string base64(const Bytes &bytes)
{
	std::string result;
	for (std::size_t i = 0; i < bytes.size(); i += 3)
	{
		unsigned word = static_cast<unsigned>(bytes[i]) << 16;
		if (i + 1 < bytes.size())
			word |= static_cast<unsigned>(bytes[i + 1]) << 8;
		if (i + 2 < bytes.size())
			word |= bytes[i + 2];
		result += alphabet[(word >> 18) & 63];
		result += alphabet[(word >> 12) & 63];
		result += i + 1 < bytes.size() ? alphabet[(word >> 6) & 63] : '=';
		result += i + 2 < bytes.size() ? alphabet[word & 63] : '=';
	}
	return result;
}
Bytes unbase64(const std::string &text)
{
	if (text.empty() || text.size() % 4 || text.size() > 4 * ((chunk_size + 2) / 3))
		throw std::runtime_error("Invalid bounded pack chunk encoding");
	Bytes bytes;
	for (std::size_t i = 0; i < text.size(); i += 4)
	{
		unsigned word = 0;
		for (std::size_t j = 0; j < 4; ++j)
		{
			auto found = alphabet.find(text[i + j]);
			if (found == std::string::npos && text[i + j] != '=')
				throw std::runtime_error("Invalid pack chunk base64");
			word = (word << 6) | static_cast<unsigned>(found == std::string::npos ? 0 : found);
		}
		bytes.push_back(static_cast<std::uint8_t>(word >> 16));
		if (text[i + 2] != '=')
			bytes.push_back(static_cast<std::uint8_t>(word >> 8));
		if (text[i + 3] != '=')
			bytes.push_back(static_cast<std::uint8_t>(word));
	}
	if (base64(bytes) != text)
		throw std::runtime_error("Noncanonical pack chunk encoding");
	return bytes;
}
} // namespace
namespace wire_detail
{
namespace
{
void check_chunk(std::size_t length, std::size_t offset, std::span<const std::uint8_t> data)
{
	if (offset >= length || offset % chunk_size)
		throw std::runtime_error("Invalid transfer chunk offset");
	if (data.size() != std::min(chunk_size, length - offset))
		throw std::runtime_error("Transfer chunk length mismatch");
}
} // namespace
ChunkBuffer::ChunkBuffer(std::size_t length, std::string hash)
    : bytes_(length), hash_(std::move(hash)), received_((length + chunk_size - 1) / chunk_size, false),
      deadlines_(received_.size(), 0)
{
}
unsigned ChunkBuffer::active(std::uint64_t now) const
{
	unsigned count = 0;
	for (std::size_t i = 0; i < received_.size(); ++i)
		if (!received_[i] && deadlines_[i] > now)
			++count;
	return count;
}
std::vector<std::size_t> ChunkBuffer::requests(std::uint64_t now, unsigned &budget, std::uint64_t retry_ms)
{
	std::vector<std::size_t> offsets;
	for (std::size_t i = 0; i < received_.size() && budget; ++i)
		if (!received_[i] && deadlines_[i] <= now)
		{
			deadlines_[i] = now + retry_ms;
			--budget;
			offsets.push_back(i * chunk_size);
		}
	return offsets;
}
void ChunkBuffer::duplicate(std::span<const std::uint8_t> original, std::size_t offset,
                            std::span<const std::uint8_t> data)
{
	check_chunk(original.size(), offset, data);
	if (!std::equal(data.begin(), data.end(), original.begin() + offset))
		throw std::runtime_error("Conflicting repeated transfer chunk");
}
bool ChunkBuffer::accept(std::size_t offset, std::span<const std::uint8_t> data)
{
	check_chunk(bytes_.size(), offset, data);
	const auto index = offset / chunk_size;
	if (received_[index])
	{
		duplicate(bytes_, offset, data);
		return false;
	}
	std::copy(data.begin(), data.end(), bytes_.begin() + offset);
	received_[index] = true;
	if (std::all_of(received_.begin(), received_.end(),
	                [](bool value)
	                {
		                return value;
	                }))
	{
		if (sha256(bytes_) != hash_)
			throw std::runtime_error("Transfer integrity check failed");
		complete_ = true;
	}
	return true;
}
bool ChunkBuffer::complete() const
{
	return complete_;
}
const Bytes &ChunkBuffer::bytes() const
{
	return bytes_;
}
Bytes ChunkBuffer::release()
{
	if (!complete_)
		throw std::runtime_error("Transfer is incomplete");
	return std::move(bytes_);
}
} // namespace wire_detail
PackSource::PackSource(std::vector<Bytes> archives)
{
	if (archives.empty() || archives.size() > 32)
		throw std::runtime_error("Select one to 32 packs for transfer");
	std::size_t total = 0;
	for (const auto &archive : archives)
	{
		if (archive.empty() || archive.size() > max_archive || archive.size() > max_total - total)
			throw std::runtime_error("Selected transfer content exceeds 64 MiB total or 32 MiB per pack");
		total += archive.size();
	}
	std::vector<Pack> packs;
	for (auto &archive : archives)
		packs.push_back(read_pack(std::move(archive)));
	*this = PackSource(std::move(packs));
}
PackSource::PackSource(std::vector<Pack> packs) : packs_(std::move(packs))
{
	if (packs_.empty() || packs_.size() > 32)
		throw std::runtime_error("Select one to 32 packs for transfer");
	offer_ = {{"schema", 1}, {"packs", Json::array()}};
	std::size_t total = 0;
	for (const auto &pack : packs_)
	{
		const auto &archive = pack.archive();
		total += archive.size();
		if (archive.empty() || archive.size() > max_archive || total > max_total)
			throw std::runtime_error("Selected transfer content exceeds 64 MiB total or 32 MiB per pack");
		offer_["packs"].push_back({{"id", pack.manifest().at("id")}, {"hash", pack.hash()}, {"size", archive.size()}});
	}
	resolve_packs(packs_);
}
Json PackSource::offer() const
{
	return offer_;
}
Json PackSource::chunk(const Json &request) const
{
	auto index = size(request.at("pack"), packs_.size() - 1),
	     offset = size(request.at("offset"), packs_.at(index).archive().size());
	const auto &bytes = packs_.at(index).archive();
	if (offset >= bytes.size() || offset % chunk_size)
		throw std::runtime_error("Pack chunk offset must be aligned and inside its archive");
	auto end = std::min(bytes.size(), offset + chunk_size);
	return {{"pack", index},
	        {"hash", offer_.at("packs").at(index).at("hash")},
	        {"offset", offset},
	        {"data", base64(Bytes(bytes.begin() + offset, bytes.begin() + end))}};
}
void PackTransfer::validate_offer(const Json &offer)
{
	if (offer.at("schema") != 1 || !offer.at("packs").is_array() || offer.at("packs").empty() ||
	    offer.at("packs").size() > 32)
		throw std::runtime_error("Invalid pack offer");
	std::set<std::string> ids;
	std::size_t total = 0;
	for (const auto &entry : offer.at("packs"))
	{
		const std::string id = entry.at("id"), hash = entry.at("hash");
		auto length = size(entry.at("size"), max_archive);
		if (!length || !ids.insert(id).second || !valid_hex_token(hash) || length > max_total - total)
			throw std::runtime_error("Invalid or oversized ordered pack offer");
		total += length;
	}
}
PackTransfer::PackTransfer(const Json &offer, std::map<std::string, Bytes> cache)
{
	validate_offer(offer);
	for (const auto &entry : offer.at("packs"))
	{
		Pending pack;
		pack.id = entry.at("id");
		pack.hash = entry.at("hash");
		pack.length = size(entry.at("size"), max_archive);
		total_ += pack.length;
		auto found = cache.find(pack.hash);
		if (found != cache.end() && found->second.size() == pack.length && sha256(found->second) == pack.hash)
		{
			auto archive = read_pack(std::move(found->second));
			if (archive.manifest().at("id") != pack.id)
				throw std::runtime_error("Cached archive does not match offered pack identity");
			pack.verified = std::move(archive);
			pack.cached = true;
			received_ += pack.length;
		}
		else
		{
			pack.buffer = wire_detail::ChunkBuffer(pack.length, pack.hash);
		}
		packs_.push_back(std::move(pack));
	}
	resolve();
}
std::vector<Json> PackTransfer::requests(std::uint64_t now, unsigned window)
{
	window = std::clamp(window, 1u, 8u);
	std::vector<Json> result;
	unsigned active = 0;
	for (const auto &pack : packs_)
		active += pack.buffer.active(now);
	unsigned budget = active < window ? window - active : 0;
	for (std::size_t index = 0; index < packs_.size() && budget; ++index)
		for (const auto offset : packs_[index].buffer.requests(now, budget, 2000))
			result.push_back({{"pack", index}, {"offset", offset}});
	return result;
}
bool PackTransfer::accept(const Json &chunk)
{
	const auto index = size(chunk.at("pack"), packs_.size() - 1);
	auto &pack = packs_.at(index);
	if (chunk.at("hash") != pack.hash)
		throw std::runtime_error("Chunk belongs to different offered content");
	const auto offset = size(chunk.at("offset"), pack.length);
	const auto bytes = unbase64(chunk.at("data"));
	if (pack.verified)
	{
		wire_detail::ChunkBuffer::duplicate(pack.verified->archive(), offset, bytes);
		return false;
	}
	if (!pack.buffer.accept(offset, bytes))
		return false;
	received_ += bytes.size();
	if (pack.buffer.complete())
	{
		auto verified = read_pack(pack.buffer.release());
		if (verified.manifest().at("id") != pack.id)
			throw std::runtime_error("Transferred archive failed hash/identity verification");
		pack.verified = std::move(verified);
		resolve();
	}
	return true;
}
void PackTransfer::resolve()
{
	if (!std::all_of(packs_.begin(), packs_.end(),
	                 [](const Pending &pack)
	                 {
		                 return pack.verified.has_value();
	                 }))
		return;
	ordered_.clear();
	for (const auto &pack : packs_)
		ordered_.push_back(*pack.verified);
	catalog_ = resolve_packs(ordered_);
	ready_ = true;
}
bool PackTransfer::ready() const
{
	return ready_;
}
std::size_t PackTransfer::received_bytes() const
{
	return received_;
}
std::size_t PackTransfer::total_bytes() const
{
	return total_;
}
const Catalog &PackTransfer::catalog() const
{
	if (!ready_)
		throw std::runtime_error("Content is not verified/ready");
	return catalog_;
}
const std::vector<Pack> &PackTransfer::packs() const
{
	if (!ready_)
		throw std::runtime_error("Content is not verified/ready");
	return ordered_;
}
std::vector<Pack> PackTransfer::uncached_packs() const
{
	if (!ready_)
		throw std::runtime_error("Content is not verified/ready");
	std::vector<Pack> result;
	for (const auto &pack : packs_)
		if (!pack.cached)
			result.push_back(*pack.verified);
	return result;
}
StateSource::StateSource(const Json &state, std::uint64_t sequence) : bytes_(json_bytes(state))
{
	if (!state.is_object() || bytes_.empty() || bytes_.size() > 4 * 1024 * 1024 || !sequence)
		throw std::runtime_error("Projection exceeds the supported 4 MiB state bound");
	offer_ = {{"sequence", sequence}, {"size", bytes_.size()}, {"hash", sha256(bytes_)}};
}
StateSource::StateSource(Bytes archive, std::uint64_t sequence) : bytes_(std::move(archive))
{
	if (bytes_.empty() || bytes_.size() > max_archive || !sequence)
		throw std::runtime_error("Private setup exceeds the 32 MiB archive bound");
	offer_ = {{"sequence", sequence}, {"size", bytes_.size()}, {"hash", sha256(bytes_)}};
}
Json StateSource::offer() const
{
	return offer_;
}
Json StateSource::chunk(const Json &request) const
{
	if (request.at("sequence") != offer_.at("sequence"))
		throw std::runtime_error("Projection request belongs to another snapshot");
	const auto offset = size(request.at("offset"), bytes_.size() - 1);
	if (offset % chunk_size)
		throw std::runtime_error("Projection chunk is not aligned");
	const auto end = std::min(offset + chunk_size, bytes_.size());
	return {{"sequence", offer_.at("sequence")},
	        {"offset", offset},
	        {"data", base64(Bytes(bytes_.begin() + offset, bytes_.begin() + end))}};
}
StateTransfer::StateTransfer(const Json &offer, bool archive) : offer_(offer), archive_(archive)
{
	const auto length = size(offer.at("size"), archive ? max_archive : 4 * 1024 * 1024);
	const auto sequence = size(offer.at("sequence"), 9007199254740991ULL);
	if (!length || !sequence || !valid_hex_token(offer.at("hash").get<std::string>()))
		throw std::runtime_error("Invalid bounded projection offer");
	buffer_ = wire_detail::ChunkBuffer(length, offer.at("hash").get<std::string>());
}
std::vector<Json> StateTransfer::requests(std::uint64_t now)
{
	std::vector<Json> result;
	const auto active = buffer_.active(now);
	unsigned budget = active < 4 ? 4 - active : 0;
	for (const auto offset : buffer_.requests(now, budget, 750))
		result.push_back({{"sequence", sequence()}, {"offset", offset}});
	return result;
}
bool StateTransfer::accept(const Json &chunk)
{
	if (chunk.at("sequence") != offer_.at("sequence"))
		return false;
	const auto offset = size(chunk.at("offset"), buffer_.bytes().size() - 1);
	const auto data = unbase64(chunk.at("data"));
	if (!buffer_.accept(offset, data))
		return false;
	if (buffer_.complete())
	{
		if (archive_)
		{
			complete_ = true;
			return true;
		}
		const auto &bytes = buffer_.bytes();
		state_ = Json::parse(bytes.begin(), bytes.end(),
		                     [](int depth, Json::parse_event_t, Json &)
		                     {
			                     if (depth > 32)
				                     throw std::runtime_error("Projection nesting exceeds supported bounds");
			                     return true;
		                     });
		if (!state_.is_object())
			throw std::runtime_error("Projection must be an object");
	}
	return true;
}
bool StateTransfer::ready() const
{
	return complete_ || state_.is_object();
}
std::uint64_t StateTransfer::sequence() const
{
	return offer_.at("sequence");
}
const Json &StateTransfer::state() const
{
	if (!ready())
		throw std::runtime_error("Projection is incomplete");
	return state_;
}
const Bytes &StateTransfer::archive() const
{
	if (!archive_ || !ready())
		throw std::runtime_error("Private setup transfer is incomplete");
	return buffer_.bytes();
}
} // namespace phoenix
