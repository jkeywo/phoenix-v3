module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

module phoenix.platform.user_paths;

namespace phoenix
{
std::string native_data_directory()
{
#ifdef _WIN32
	PWSTR folder = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
		throw std::runtime_error("Cannot locate the local application data folder; specify --data explicitly");
	const auto root = std::filesystem::path(folder);
	CoTaskMemFree(folder);
	const auto encoded = (root / "PhoenixV3").u8string();
	return {encoded.begin(), encoded.end()};
#elif defined(__linux__)
	if (const auto *root = std::getenv("XDG_DATA_HOME"); root && std::filesystem::path(root).is_absolute())
		return (std::filesystem::path(root) / "phoenix-v3").string();
	if (const auto *home = std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
		return (std::filesystem::path(home) / ".local/share/phoenix-v3").string();
	throw std::runtime_error("Cannot locate the local application data folder; specify --data explicitly");
#else
	return "userdata";
#endif
}
} // namespace phoenix
