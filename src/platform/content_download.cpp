module;
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#else
#include <thread>
#include <mutex>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
#endif
module phoenix.transport;
namespace phoenix
{
namespace
{
constexpr std::size_t limit = 32 * 1024 * 1024;
using Data = std::vector<std::uint8_t>;
#ifdef __EMSCRIPTEN__
class Download final : public ContentDownload
{
	emscripten_fetch_t *fetch_ = nullptr;
	std::optional<Data> result_;

public:
	explicit Download(const std::string &url)
	{
		emscripten_fetch_attr_t attr;
		emscripten_fetch_attr_init(&attr);
		attr.requestMethod[0] = 'G';
		attr.requestMethod[1] = 'E';
		attr.requestMethod[2] = 'T';
		attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
		attr.timeoutMSecs = 10000;
		attr.userData = this;
		attr.onsuccess = [](emscripten_fetch_t *f)
		{
			auto *self = static_cast<Download *>(f->userData);
			self->result_ = f->numBytes <= limit && f->status == 200 ? Data(f->data, f->data + f->numBytes) : Data{};
			self->fetch_ = nullptr;
			emscripten_fetch_close(f);
		};
		attr.onerror = [](emscripten_fetch_t *f)
		{
			auto *self = static_cast<Download *>(f->userData);
			self->result_ = Data{};
			self->fetch_ = nullptr;
			emscripten_fetch_close(f);
		};
		attr.onprogress = [](emscripten_fetch_t *f)
		{
			if (f->totalBytes > limit || f->dataOffset + f->numBytes > limit)
			{
				auto *self = static_cast<Download *>(f->userData);
				self->result_ = Data{};
				self->fetch_ = nullptr;
				emscripten_fetch_close(f);
			}
		};
		fetch_ = emscripten_fetch(&attr, url.c_str());
		if (!fetch_)
			result_ = Data{};
	}
	~Download() override
	{
		if (fetch_)
			emscripten_fetch_close(fetch_);
	}
	std::optional<Data> poll() override
	{
		return std::move(result_);
	}
};
#else
Data receive(const std::string &url)
{
	Data bytes;
#ifdef _WIN32
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	struct Handle
	{
		HINTERNET value;
		~Handle()
		{
			if (value)
				WinHttpCloseHandle(value);
		}
	};
	const int length =
	    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url.data(), static_cast<int>(url.size()), nullptr, 0);
	if (!length)
		return {};
	std::wstring wide(length, L' ');
	MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), wide.data(), length);
	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts))
		return {};
	Handle session{WinHttpOpen(L"PhoenixV3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
	                           WINHTTP_NO_PROXY_BYPASS, 0)};
	if (!session.value)
		return {};
	WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 5000);
	const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
	Handle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
	const auto path = (parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/") +
	                  (parts.dwExtraInfoLength ? std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength) : L"");
	Handle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
	                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                  parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
	if (!request.value ||
	    !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(request.value, nullptr))
		return {};
	DWORD status = 0, size = sizeof(status);
	if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) ||
	    status != 200)
		return {};
	for (;;)
	{
		if (std::chrono::steady_clock::now() > deadline)
			return {};
		DWORD available = 0, received = 0;
		if (!WinHttpQueryDataAvailable(request.value, &available))
			return {};
		if (!available)
			break;
		if (available > limit - bytes.size())
			return {};
		const auto offset = bytes.size();
		bytes.resize(offset + available);
		if (!WinHttpReadData(request.value, bytes.data() + offset, available, &received))
			return {};
		bytes.resize(offset + received);
	}
#else
	static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (initialized != CURLE_OK)
		return {};
	auto *curl = curl_easy_init();
	if (!curl)
		return {};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
	curl_easy_setopt(
	    curl, CURLOPT_WRITEFUNCTION,
	    +[](char *data, std::size_t size, std::size_t count, void *user) -> std::size_t
	    {
		    auto &output = *static_cast<Data *>(user);
		    if (size && count > (limit - output.size()) / size)
			    return 0;
		    const auto length = size * count;
		    output.insert(output.end(), data, data + length);
		    return length;
	    });
	const auto status = curl_easy_perform(curl);
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(curl);
	if (status != CURLE_OK || code != 200)
		return {};
#endif
	return bytes;
}
class Download final : public ContentDownload
{
	struct State
	{
		std::mutex mutex;
		std::optional<Data> result;
	};
	std::shared_ptr<State> state_ = std::make_shared<State>();

public:
	explicit Download(std::string url)
	{
		std::thread(
		    [state = state_, url = std::move(url)]
		    {
			    Data result;
			    try
			    {
				    result = receive(url);
			    }
			    catch (...)
			    {
			    }
			    std::lock_guard lock(state->mutex);
			    state->result = std::move(result);
		    })
		    .detach();
	}
	std::optional<Data> poll() override
	{
		std::lock_guard lock(state_->mutex);
		return std::move(state_->result);
	}
};
#endif
} // namespace
std::unique_ptr<ContentDownload> download_content(const std::string &url)
{
	if (url.size() > 2048 || (!url.starts_with("https://") && !url.starts_with("http://")))
		return {};
	return std::make_unique<Download>(url);
}
} // namespace phoenix
