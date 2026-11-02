module;
#include <algorithm>
#include <set>
#include <cstdint>
#include <cmath>
#include <map>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <stdexcept>
#include <vector>
#include <utility>

module phoenix.wire;
import phoenix.content.verification;

namespace phoenix
{
namespace
{
constexpr std::size_t chunk_size = 12 * 1024, max_archive = content_archive_limit, max_total = 256 * 1024 * 1024;
const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::uint64_t bounded_number(const Json &value, std::uint64_t maximum)
{
	if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
		throw std::runtime_error("Invalid pack transfer size/offset");
	auto number = value.get<std::uint64_t>();
	if (number > maximum)
		throw std::runtime_error("Pack transfer exceeds supported bounds");
	return number;
}
std::size_t size(const Json &value, std::size_t maximum)
{
	return static_cast<std::size_t>(bounded_number(value, maximum));
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
constexpr std::size_t state_limit = 4 * 1024 * 1024;
// Public SAX API bounds nesting before the CBOR reader can recurse further.
// Do not trust CBOR's declared container lengths for allocation.
class StateReader final : public nlohmann::json_sax<Json>
{
	struct Frame
	{
		Json value;
		std::string key;
	};
	std::vector<Frame> frames_;
	std::size_t nodes_ = 0;
	bool append(Json value)
	{
		if (++nodes_ > state_limit)
			return false;
		if (frames_.empty())
			result = std::move(value);
		else if (frames_.back().value.is_array())
			frames_.back().value.push_back(std::move(value));
		else
			frames_.back().value[frames_.back().key] = std::move(value);
		return true;
	}
	bool start(bool object, std::size_t elements)
	{
		if (frames_.size() >= 32 || (elements != std::numeric_limits<std::size_t>::max() && elements > state_limit))
			return false;
		frames_.push_back({object ? Json::object() : Json::array(), {}});
		return true;
	}
	bool end()
	{
		auto value = std::move(frames_.back().value);
		frames_.pop_back();
		return append(std::move(value));
	}

public:
	Json result;
	bool null() override
	{
		return append(nullptr);
	}
	bool boolean(bool value) override
	{
		return append(value);
	}
	bool number_integer(number_integer_t value) override
	{
		return append(value);
	}
	bool number_unsigned(number_unsigned_t value) override
	{
		return append(value);
	}
	bool number_float(number_float_t value, const string_t &) override
	{
		return append(value);
	}
	bool string(string_t &value) override
	{
		return append(std::move(value));
	}
	bool binary(binary_t &) override
	{
		return false;
	}
	bool start_object(std::size_t elements) override
	{
		return start(true, elements);
	}
	bool key(string_t &value) override
	{
		if (frames_.back().value.contains(value))
			return false;
		frames_.back().key = std::move(value);
		return true;
	}
	bool end_object() override
	{
		return end();
	}
	bool start_array(std::size_t elements) override
	{
		return start(false, elements);
	}
	bool end_array() override
	{
		return end();
	}
	bool parse_error(std::size_t, const std::string &, const nlohmann::detail::exception &) override
	{
		return false;
	}
};
void validate_state(const Json &value, unsigned depth = 0)
{
	if (depth > 32 || (value.is_structured() && depth >= 32) || value.is_binary() || value.is_discarded() ||
	    (value.is_number_float() && !std::isfinite(value.get<double>())))
		throw std::runtime_error("Invalid or excessively nested projection value");
	if (value.is_structured())
		for (const auto &child : value)
			validate_state(child, depth + 1);
}
Bytes state_bytes(const Json &value)
{
	validate_state(value);
	auto bytes = Json::to_cbor(value);
	if (bytes.empty() || bytes.size() > state_limit)
		throw std::runtime_error("Projection exceeds the supported 4 MiB binary state bound");
	return bytes;
}
Json read_state_bytes(const Bytes &bytes)
{
	StateReader reader;
	if (!Json::sax_parse(bytes, &reader, Json::input_format_t::cbor))
		throw std::runtime_error("Invalid bounded CBOR projection");
	return std::move(reader.result);
}
bool binary_equal(const Json &before, const Json &after)
{
	// JSON numeric equality equates 1 with 1.0 and -0.0 with 0.0. A binary
	// snapshot's digest must also preserve their distinct CBOR representations.
	if (before.is_number() && after.is_number())
		return before.is_number_float() == after.is_number_float() && before == after &&
		       (!before.is_number_float() || std::signbit(before.get<double>()) == std::signbit(after.get<double>()));
	if (before.is_structured() && before.type() == after.type())
	{
		if (before.size() != after.size())
			return false;
		auto a = before.begin(), b = after.begin();
		for (; a != before.end(); ++a, ++b)
			if ((before.is_object() && a.key() != b.key()) || !binary_equal(*a, *b))
				return false;
		return true;
	}
	return before == after;
}
void state_diff(const Json &before, const Json &after, const std::string &path, Json &changes)
{
	// Skip unchanged branches without allocating paths or serialising each leaf.
	if (binary_equal(before, after))
		return;
	if (before.is_object() && after.is_object())
	{
		const auto child_path = [&](const std::string &key)
		{
			std::string escaped;
			for (char c : key)
				escaped += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
			return path + '/' + escaped;
		};
		for (const auto &[key, value] : before.items())
			if (!after.contains(key))
				changes.push_back({{"op", "remove"}, {"path", child_path(key)}});
		for (const auto &[key, value] : after.items())
			if (before.contains(key))
				state_diff(before.at(key), value, child_path(key), changes);
			else
				changes.push_back({{"op", "add"}, {"path", child_path(key)}, {"value", value}});
	}
	else if (before.is_array() && after.is_array())
	{
		for (std::size_t i = 0; i < std::min(before.size(), after.size()); ++i)
			state_diff(before[i], after[i], path + '/' + std::to_string(i), changes);
		for (std::size_t i = before.size(); i > after.size(); --i)
			changes.push_back({{"op", "remove"}, {"path", path + '/' + std::to_string(i - 1)}});
		for (std::size_t i = before.size(); i < after.size(); ++i)
			changes.push_back({{"op", "add"}, {"path", path + '/' + std::to_string(i)}, {"value", after[i]}});
	}
	else
		changes.push_back({{"op", "replace"}, {"path", path}, {"value", after}});
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
			throw std::runtime_error("Selected transfer content exceeds 256 MiB total or 128 MiB per pack");
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
			throw std::runtime_error("Selected transfer content exceeds 256 MiB total or 128 MiB per pack");
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
StateSource::StateSource(const Json &state, std::uint64_t sequence, const StateSource *baseline) : state_(state)
{
	if (!state.is_object() || !sequence || sequence > 9007199254740991ULL)
		throw std::runtime_error("Invalid projection state/sequence");
	bytes_ = state_bytes(state);
	offer_ = {{"sequence", sequence}, {"encoding", "cbor-v1"}, {"state_hash", sha256(bytes_)}};
	if (baseline && baseline->state_.is_object() && baseline->offer_.at("sequence") < sequence)
	{
		auto changes = Json::array();
		state_diff(baseline->state_, state, "", changes);
		auto delta = Json::to_cbor(changes);
		if (delta.size() < bytes_.size())
		{
			// The patch wrapper can be deeper than an otherwise valid full state.
			try
			{
				validate_state(changes);
				bytes_ = std::move(delta);
				offer_["encoding"] = "cbor-patch-v1";
				offer_["base_sequence"] = baseline->offer_.at("sequence");
				offer_["base_hash"] = baseline->offer_.at("state_hash");
			}
			catch (const std::runtime_error &)
			{
				// A valid full state remains the recovery representation.
			}
		}
	}
	offer_["size"] = bytes_.size();
	offer_["hash"] = sha256(bytes_);
}
StateSource::StateSource(Bytes archive, std::uint64_t sequence) : bytes_(std::move(archive))
{
	if (bytes_.empty() || bytes_.size() > max_archive || !sequence)
		throw std::runtime_error("Private setup exceeds the 128 MiB archive bound");
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
bool StateTransfer::has_base(const Json &offer, const StateTransfer *baseline)
{
	return offer.value("encoding", std::string{}) != "cbor-patch-v1" ||
	       (baseline && baseline->ready() && !baseline->archive_ && offer.at("base_sequence") == baseline->sequence() &&
	        offer.at("base_hash") == baseline->offer_.at("state_hash"));
}
StateTransfer::StateTransfer(const Json &offer, bool archive, const StateTransfer *baseline)
    : offer_(offer), archive_(archive)
{
	const auto length = size(offer.at("size"), archive ? max_archive : 4 * 1024 * 1024);
	const auto sequence = bounded_number(offer.at("sequence"), 9007199254740991ULL);
	if (!length || !sequence || !valid_hex_token(offer.at("hash").get<std::string>()))
		throw std::runtime_error("Invalid bounded projection offer");
	if (!archive)
	{
		const auto encoding = offer.value("encoding", std::string{});
		if ((encoding != "cbor-v1" && encoding != "cbor-patch-v1") ||
		    !valid_hex_token(offer.at("state_hash").get<std::string>()))
			throw std::runtime_error("Unsupported projection encoding");
		if (!has_base(offer, baseline))
			throw std::runtime_error("Projection baseline is unavailable");
		if (encoding == "cbor-patch-v1")
		{
			if (bounded_number(offer.at("base_sequence"), 9007199254740991ULL) >= sequence)
				throw std::runtime_error("Projection baseline must precede its update");
			baseline_ = baseline->state();
		}
	}
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
		auto candidate = read_state_bytes(buffer_.bytes());
		if (offer_.at("encoding") == "cbor-patch-v1")
		{
			if (!candidate.is_array())
				throw std::runtime_error("Projection delta must contain operations");
			for (const auto &operation : candidate)
			{
				const std::string op = operation.at("op"), path = operation.at("path");
				// Copy/move could amplify a bounded input or retain withdrawn evidence.
				if ((op != "add" && op != "remove" && op != "replace") ||
				    std::count(path.begin(), path.end(), '/') > 32)
					throw std::runtime_error("Unsupported projection delta operation");
			}
			candidate = baseline_.patch(candidate);
		}
		if (!candidate.is_object())
			throw std::runtime_error("Projection must be an object");
		if (sha256(state_bytes(candidate)) != offer_.at("state_hash").get<std::string>())
			throw std::runtime_error("Reconstructed projection integrity check failed");
		state_ = std::move(candidate);
		baseline_ = nullptr;
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
