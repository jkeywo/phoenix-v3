module;
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.ui.tactical_widgets;
export import phoenix.content.catalog;

export import phoenix.console;
export import phoenix.input;
export namespace phoenix
{
// Owns the separately granted tactical Console model embedded beside Helm. It
// cannot widen the primary observation or bypass ordinary command admission.
class TacticalWidgets
{
public:
	void refresh(const ConsoleModel &primary, const std::shared_ptr<SessionPort> &, ConsoleId route);
	std::vector<std::pair<std::string, std::string>> weapon_controls() const;
	std::vector<std::pair<std::string, std::string>> target_readouts(ConsoleModel &primary, const Catalog &,
	                                                                 bool detailed = false);
	bool select_contact(ConsoleModel &primary, EntityId);
	ConsoleModel &model()
	{
		return model_;
	}
	const std::string &console() const
	{
		return console_;
	}

private:
	ConsoleModel model_;
	std::string console_;
};
} // namespace phoenix
