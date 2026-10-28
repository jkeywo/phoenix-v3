module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.ui.information_widgets;
export import phoenix.content.catalog;

export import phoenix.console;
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
