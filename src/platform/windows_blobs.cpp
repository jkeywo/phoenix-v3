module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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
#include "../network/blob_internal.hpp"

namespace phoenix
{
namespace
{
class WindowsBlobs final : public BlobStore
{
	std::filesystem::path root_;
	std::future<BlobResult> pending_;
	bool ready_ = false;
	std::filesystem::path path(const std::string &key) const
	{
		return root_ / (sha256(Bytes(key.begin(), key.end())) + ".blob");
	}

public:
	explicit WindowsBlobs(const std::string &location)
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
		auto file = path(key), temporary = root_ / (random_token() + ".tmp");
		pending_ = std::async(
		    std::launch::async,
		    [request, key, bytes, file, temporary]
		    {
			    BlobResult result;
			    result.request = request;
			    result.key = key;
			    HANDLE handle = INVALID_HANDLE_VALUE;
			    try
			    {
				    auto encoded = blobs::encode(key, *bytes);
				    handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
				                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
				    if (handle == INVALID_HANDLE_VALUE)
					    throw std::runtime_error("Cannot create persistent blob temporary file");
				    DWORD written = 0;
				    if (!WriteFile(handle, encoded.data(), static_cast<DWORD>(encoded.size()), &written, nullptr) ||
				        written != encoded.size() || !FlushFileBuffers(handle))
					    throw std::runtime_error("Persistent blob write/flush failed");
				    CloseHandle(handle);
				    handle = INVALID_HANDLE_VALUE;
				    if (!MoveFileExW(temporary.c_str(), file.c_str(),
				                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
					    throw std::runtime_error("Cannot atomically replace persistent blob");
			    }
			    catch (const std::exception &error)
			    {
				    result.error = error.what();
				    if (handle != INVALID_HANDLE_VALUE)
					    CloseHandle(handle);
				    std::error_code ignored;
				    std::filesystem::remove(temporary, ignored);
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
	return std::make_unique<WindowsBlobs>(location);
}
} // namespace phoenix
