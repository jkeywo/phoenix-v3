module;
#include <string>

export module phoenix.platform.display_identity;

export namespace phoenix
{
std::string native_monitor_identity(int x, int y, const std::string &fallback);
} // namespace phoenix
