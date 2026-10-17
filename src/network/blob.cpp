module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.blobs;
#include "blob_internal.hpp"

namespace phoenix
{
bool BlobStore::write(std::uint64_t request, const std::string &key, std::shared_ptr<const Bytes> bytes)
{
	if (!bytes)
		throw std::invalid_argument("Persistent blob payload is missing");
	return write(request, key, *bytes);
}
} // namespace phoenix

namespace phoenix::blobs
{
void verify_key(const std::string &key)
{
	if (!valid_identifier(key))
		throw std::runtime_error("Invalid persistent blob key");
}
Bytes encode(const std::string &key, const Bytes &bytes)
{
	verify_key(key);
	if (bytes.size() > max_bytes)
		throw std::runtime_error("Persistent blob exceeds 32 MiB");
	const auto text = Json{{"schema", 1}, {"key", key}, {"hash", sha256(bytes)}, {"size", bytes.size()}}.dump();
	Bytes header(text.begin(), text.end());
	header.push_back('\n');
	header.insert(header.end(), bytes.begin(), bytes.end());
	return header;
}
Bytes decode(const std::string &key, const Bytes &encoded)
{
	verify_key(key);
	if (encoded.size() > max_bytes + 1024)
		throw std::runtime_error("Persistent blob exceeds bounds");
	auto end = std::find(encoded.begin(), encoded.end(), '\n');
	if (end == encoded.end() || std::distance(encoded.begin(), end) > 1024)
		throw std::runtime_error("Invalid persistent blob header");
	auto header = Json::parse(encoded.begin(), end);
	Bytes bytes(std::next(end), encoded.end());
	if (header.at("schema") != 1 || header.at("key") != key || header.at("size") != bytes.size() ||
	    header.at("hash") != sha256(bytes))
		throw std::runtime_error("Persistent blob identity/integrity mismatch");
	return bytes;
}
} // namespace phoenix::blobs
