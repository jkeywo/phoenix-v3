module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <set>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.ui.information_widgets;
export import phoenix.content.catalog;

export import phoenix.console;
export import phoenix.ui.surface_host;
export namespace phoenix::message_ui
{
std::string asset(const std::string &relative);
ConsoleMessageView conversation_message(const Json &message, const std::string &id, const Catalog &catalog);
std::string markup(const ConsoleMessageView &message, bool compact = false);
// Keep existing rows mounted. Arriving messages neither recreate focused siblings
// nor reset the containing history's scroll position.
void refresh(Rml::ElementDocument &document, Rml::Element &panel, const std::vector<ConsoleMessageView> &messages,
             const Catalog *catalog, std::vector<std::pair<std::string, std::string>> &readouts,
             const std::string &prefix = {}, bool compact = false);
} // namespace phoenix::message_ui

export namespace phoenix
{
struct MessageHistoryFrame
{
	ConsoleDocument &dom;
	Rml::Context &context;
	ConsolePanelOutput output;
	ConsoleRegion region;
	const Catalog *catalog;
	std::string prefix, scope;
	bool visible, pseudo;
	double now, seconds;
	std::string id(const std::string &name) const
	{
		return prefix + name;
	}
};
struct MessageHistoryNotice
{
	std::string announcement;
	bool cue = false;
};
class MessageHistoryPanel
{
public:
	std::optional<bool> action(ConsoleDocument &, const std::string &);
	MessageHistoryNotice refresh(const std::vector<ConsoleMessageView> &, MessageHistoryFrame);
	void advance_clock(Rml::Context &, double now);
	void reset_scope();
	bool open() const
	{
		return messages_open_;
	}
	bool refresh_due(double now) const
	{
		return !message_queue_.empty() && now >= message_until_;
	}
	bool stable(double now) const
	{
		return !messages_open_ && !refresh_due(now);
	}

private:
	MessageHistory message_history_;
	bool messages_open_ = false;
	std::string message_filter_, message_scope_;
	std::vector<ConsoleMessageView> message_queue_;
	double message_until_ = 0, message_last_time_ = 0;
};
} // namespace phoenix
