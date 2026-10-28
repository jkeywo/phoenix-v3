module;
#include <filesystem>
#include <future>
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
namespace
{
class NativeBlobs final : public BlobStore
{
	std::filesystem::path root_;
	std::future<BlobResult> pending_;
	bool ready_ = false;
	std::filesystem::path path(const std::string &key) const
	{
		return root_ / (sha256(Bytes(key.begin(), key.end())) + ".blob");
	}

public:
	explicit NativeBlobs(const std::string &location)
	    : root_(std::filesystem::absolute(std::filesystem::u8path(location)))
	{
		pending_ = std::async(std::launch::async,
		                      [root = root_]
		                      {
			                      BlobResult result;
			                      result.opened = true;
			                      try
			                      {
				                      std::filesystem::create_directories(root);
			                      }
			                      catch (const std::exception &error)
			                      {
				                      result.error = error.what();
			                      }
			                      return result;
		                      });
	}
	bool read(std::uint64_t request, const std::string &key) override
	{
		if (!ready_ || pending_.valid())
			return false;
		blobs::verify_key(key);
		auto file = path(key);
		pending_ = std::async(std::launch::async,
		                      [request, key, file]
		                      {
			                      BlobResult result;
			                      result.request = request;
			                      result.key = key;
			                      try
			                      {
				                      if (!std::filesystem::exists(file))
					                      return result;
				                      const auto encoded = file.u8string();
				                      result.bytes =
				                          blobs::decode(key, read_file(std::string(encoded.begin(), encoded.end()),
				                                                       blobs::max_bytes + 1024));
			                      }
			                      catch (const std::exception &error)
			                      {
				                      result.error = error.what();
			                      }
			                      return result;
		                      });
		return true;
	}
	bool write(std::uint64_t request, const std::string &key, const Bytes &bytes) override
	{
		if (!ready_ || pending_.valid())
			return false;
		blobs::verify_key(key);
		if (bytes.size() > blobs::max_bytes)
			throw std::runtime_error("Persistent blob exceeds bounds");
		return write(request, key, std::make_shared<const Bytes>(bytes));
	}
	bool write(std::uint64_t request, const std::string &key, std::shared_ptr<const Bytes> bytes) override
	{
		if (!ready_ || pending_.valid())
			return false;
		if (!bytes)
			throw std::invalid_argument("Persistent blob payload is missing");
		blobs::verify_key(key);
		if (bytes->size() > blobs::max_bytes)
			throw std::runtime_error("Persistent blob exceeds bounds");
		const auto encoded_path = path(key).u8string();
		const std::string file(encoded_path.begin(), encoded_path.end());
		pending_ =
		    std::async(std::launch::async,
		               [request, key, bytes, file]
		               {
			               BlobResult result;
			               result.request = request;
			               result.key = key;
			               try
			               {
				               auto encoded = blobs::encode(key, *bytes);
				               StagedFileReplacement(file, encoded, FileWriteMode::Flushed, "Persistent blob").commit();
			               }
			               catch (const std::exception &error)
			               {
				               result.error = error.what();
			               }
			               return result;
		               });
		return true;
	}
	std::optional<BlobResult> poll() override
	{
		if (!pending_.valid() || pending_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
			return {};
		auto result = pending_.get();
		if (result.opened)
			ready_ = result.error.empty();
		return result;
	}
};
} // namespace
std::unique_ptr<BlobStore> open_blob_store(const std::string &location)
{
	return std::make_unique<NativeBlobs>(location);
}
} // namespace phoenix
