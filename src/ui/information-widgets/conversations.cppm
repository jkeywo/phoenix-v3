module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.ui.conversation_panel;
export import phoenix.ui.surface_host;
export import phoenix.console;
export import phoenix.content.ui;
export import phoenix.content.catalog;
export namespace phoenix
{
struct ConversationFrame
{
	ConsoleDocument &dom;
	Rml::Context &context;
	ConsolePanelOutput output;
	ConsoleRegion region;
	const UiResolved &layout;
	const Catalog *catalog;
	std::string prefix;
	bool lobby, reduced_motion, history_visible, compose_visible, whole_visible;
	std::string id(const std::string &name) const
	{
		return prefix + name;
	}
};
// Owns only conversation browsing, read acknowledgement and mounted-view signatures.
// Frame capabilities are borrowed for the synchronous call and never retained.
class ConversationPanel
{
public:
	void refresh(ConsoleModel &, ConversationFrame);
	void fit(const ConsoleModel &, ConversationFrame);
	bool action(ConsoleModel &, ConsoleDocument &, const std::string &prefix, const std::string &action);
	void invalidate_text();

private:
	std::string comms_widget_, comms_list_signature_, comms_choice_signature_, comms_header_signature_;
	std::string comms_board_signature_, incoming_;
	std::uint64_t comms_visible_thread_ = 0, comms_read_thread_ = 0, comms_read_message_ = 0;
	bool comms_list_only_ = false;
	std::vector<std::pair<std::string, std::string>> comms_controls_;
};
} // namespace phoenix
