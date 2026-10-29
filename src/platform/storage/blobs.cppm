module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.blobs;
export import phoenix.data;

export namespace phoenix
{
// Opening completion has opened=true; operation completions echo request/key.
// Nonempty error means failure. A successful read has bytes only when the key
// exists (an empty Bytes is present data); writes and missing reads have no bytes.
struct BlobResult
{
	bool opened = false;
	std::uint64_t request = 0;
	std::string key, error;
	std::optional<Bytes> bytes;
};
// Poll and consume a successful opening result before issuing requests. Native
// and browser adapters admit one operation at a time: consume its completion
// before submitting the next. A failed opening leaves the store unavailable.
// Calls are intended to be driven by one owner; no concurrent-call guarantee.
class BlobStore
{
public:
	virtual ~BlobStore() = default;
	// true means admitted, not completed; false means not ready or busy, with
	// no operation queued. Validation may be skipped when admission is rejected.
	// Invalid keys (see valid_identifier) and payloads above content_archive_limit
	// throw synchronously. Resource/dispatch failures may also throw; storage and
	// decoding failures after admission arrive in BlobResult.error.
	virtual bool read(std::uint64_t request, const std::string &key) = 0;
	virtual bool write(std::uint64_t request, const std::string &key, const Bytes &bytes) = 0;
	// Immutable ownership avoids a payload copy when an adapter retains work
	// asynchronously. Payload must be non-null (otherwise invalid_argument);
	// adapters may reject admission before checking this precondition. The Bytes
	// overload copies/consumes the input during the call; callers can then reuse it.
	virtual bool write(std::uint64_t request, const std::string &key, std::shared_ptr<const Bytes> bytes);
	// nullopt means no completion is available. Consuming a result releases
	// backpressure; consuming a successful opening also enables admission.
	virtual std::optional<BlobResult> poll() = 0;
};
// Begins asynchronous opening. Poll for its result; construction/setup may throw.
// Native: portable data directory. Browser: origin-local IndexedDB namespace.
std::unique_ptr<BlobStore> open_blob_store(const std::string &location);
} // namespace phoenix
