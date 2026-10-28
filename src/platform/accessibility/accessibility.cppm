module;
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
export module phoenix.platform.accessibility;
export import phoenix.ui.interaction;
export namespace phoenix
{
struct SemanticSnapshot
{
	std::vector<SemanticControl> controls;
	std::shared_ptr<void> owner;
	std::uint64_t generation = 0;
	std::string status, settings_label;
	bool announce = false;
};
struct AccessibilityActions
{
	std::vector<std::string> actions, focuses;
	std::vector<std::pair<std::string, std::string>> values;
	bool values_first = false;
};
struct AccessibilityProbe
{
	std::function<SemanticSnapshot()> snapshot;
	std::function<void(const std::string &)> focus;
	std::function<bool(const std::string &, const std::string &)> set_value;
	std::function<bool(const std::string &)> invoke;
	std::function<bool()> at_home;
	std::vector<std::string> editable_ids;
	bool native_menu = false;
};
void publish_accessibility(const SemanticSnapshot &);
AccessibilityActions take_accessibility_actions(const SemanticSnapshot &);
void focus_semantic_text(const std::string &);
bool semantic_actions_pending();
void install_native_accessibility(void *window);
void remove_native_accessibility(void *window);
bool probe_accessibility(const AccessibilityProbe &);
} // namespace phoenix
