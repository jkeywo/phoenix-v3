module;
#include <iostream>
#include <string>

module phoenix.presentation;

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
void publish_semantics(const Application &)
{
}
void drain_semantic_actions(Application &)
{
}
bool probe_native_accessibility(Application &)
{
	return false;
}
} // namespace phoenix
