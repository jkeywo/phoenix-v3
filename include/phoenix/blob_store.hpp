#pragma once
#include <phoenix/content.hpp>
#include <memory>
#include <optional>
namespace phoenix {
struct BlobResult {
    bool opened = false;
    std::uint64_t request = 0;
    std::string key, error;
    std::optional<Bytes> bytes;
};
class BlobStore {
public:
    virtual ~BlobStore() = default;
    virtual bool read(std::uint64_t request, const std::string& key) = 0;
    virtual bool write(std::uint64_t request, const std::string& key, const Bytes& bytes) = 0;
    virtual std::optional<BlobResult> poll() = 0;
};
// Native: portable data directory. Browser: origin-local IndexedDB namespace.
std::unique_ptr<BlobStore> open_blob_store(const std::string& location);
}
