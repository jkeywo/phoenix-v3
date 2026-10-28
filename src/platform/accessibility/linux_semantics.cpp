module;
#include <iostream>
#include <string>

module phoenix.platform.accessibility;

namespace phoenix
{
// The Windows UIA provider cannot be used on Linux. Keep its contract probe
// explicitly unsuccessful until a native AT-SPI provider is implemented.
void install_native_accessibility(void *)
{
	std::cerr << "Linux accessibility: AT-SPI screen-reader integration is not implemented\n";
}
void remove_native_accessibility(void *)
{
}
void focus_semantic_text(const std::string &)
{
}
void publish_accessibility(const SemanticSnapshot &)
{
}
bool semantic_actions_pending()
{
	return false;
}
AccessibilityActions take_accessibility_actions(const SemanticSnapshot &)
{
	return {};
}
bool probe_accessibility(const AccessibilityProbe &)
{
	return false;
}
} // namespace phoenix
