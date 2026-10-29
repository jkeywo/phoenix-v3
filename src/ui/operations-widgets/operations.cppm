module;
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
export module phoenix.ui.operations_widgets;
export import phoenix.content.catalog;

export import phoenix.console;
export namespace phoenix
{
struct WidgetPanel
{
	std::string markup;
	std::vector<std::pair<std::string, std::string>> readouts;
	std::string structure() const;
};
struct OperationsWidgetContext
{
	ConsoleModel &model;
	const Catalog *catalog = nullptr;
	std::string prefix, component;
	std::function<std::string(const std::string &, const std::string &)> button;
	std::function<std::string(const std::string &)> asset;
	std::function<bool(const std::string &)> component_active;
	std::function<bool(const std::string &)> select_page;
	std::string controller;
	std::optional<std::set<std::string>> operations;
	Json board(const std::string &name) const;
	bool owns(const std::string &operation, const std::string &equipment = {}) const;
	std::string id(const std::string &name) const
	{
		return prefix + name;
	}
};
// Owns browsing/notice state for operations panels. It never owns Application,
// document geometry, Console authority or another widget's state.
class OperationsWidgets
{
public:
	OperationsWidgets();
	~OperationsWidgets();
	OperationsWidgets(const OperationsWidgets &) = delete;
	OperationsWidgets &operator=(const OperationsWidgets &) = delete;
	WidgetPanel render(const std::string &component, OperationsWidgetContext);
	bool action(const OperationsWidgetContext &, const std::string &);
	bool away_attention(const ConsoleModel &) const;

private:
	struct State;
	std::unique_ptr<State> state_;
	std::map<std::string, std::unique_ptr<State>> scoped_;
	State &state(const OperationsWidgetContext &);
};
} // namespace phoenix
