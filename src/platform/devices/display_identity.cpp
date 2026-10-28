module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <string>

module phoenix.platform.display_identity;

namespace phoenix
{
std::string native_monitor_identity(int x, int y, const std::string &fallback)
{
#ifdef _WIN32
	for (DWORD i = 0;; ++i)
	{
		DISPLAY_DEVICEW adapter{};
		adapter.cb = sizeof(adapter);
		if (!EnumDisplayDevicesW(nullptr, i, &adapter, 0))
			break;
		DEVMODEW mode{};
		mode.dmSize = sizeof(mode);
		if (!EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &mode) || mode.dmPosition.x != x ||
		    mode.dmPosition.y != y)
			continue;
		DISPLAY_DEVICEW monitor{};
		monitor.cb = sizeof(monitor);
		if (EnumDisplayDevicesW(adapter.DeviceName, 0, &monitor, EDD_GET_DEVICE_INTERFACE_NAME))
		{
			const std::wstring value = monitor.DeviceID;
			const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
			                                     nullptr, nullptr);
			std::string identity(size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), identity.data(), size,
			                    nullptr, nullptr);
			if (!identity.empty())
				return identity;
		}
	}
#endif
	return fallback;
}
} // namespace phoenix
