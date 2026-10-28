module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.ui;
export import phoenix.data;
export import phoenix.content.catalog;

export namespace phoenix
{
// Presentation-only definitions. Components refer to existing permitted widgets/panels.
struct UiRect
{
	float x = 0, y = 0, width = 0, height = 0;
};
// Structural permissions, independent of transient readiness, damage or resource values.
struct UiComponentSpec
{
	std::string id, label, feature;
};
struct UiAvailability
{
	std::string profile;
	int stars = 0;
	std::set<std::string> actions, controllers, bindings, systems, features;
	bool operator==(const UiAvailability &) const;
};
struct UiNode
{
	std::string id, kind = "column", label, component;
	float minimum_width = 44, minimum_height = 44, preferred_width = 0, preferred_height = 0;
	float stretch = 1, gap = 12, padding = 8, aspect = 0;
	std::string align = "stretch";
	std::string region;
	float circle_x = .5f, circle_y = .5f, circle_radius = .35f;
	UiRect rect{0, 0, 1000, 1000};
	Json style = Json::object();
	std::vector<Json> conditions;
	std::vector<UiNode> children;
};
struct UiLayout
{
	UiNode root;
	std::optional<UiNode> compact, header;
};
struct UiTab
{
	std::string id, label;
	bool enabled = true, attention = false;
};
class UiTabs
{
public:
	bool update(std::vector<UiTab> tabs);
	bool select(const std::string &id);
	const std::string &selected() const
	{
		return selected_;
	}
	const std::vector<UiTab> &items() const
	{
		return items_;
	}

private:
	std::string selected_;
	std::vector<UiTab> items_;
};
struct UiPlacement
{
	std::string id, component, page;
	UiRect bounds;
	bool visible = true;
	Json style = Json::object();
};
struct UiResolved
{
	UiNode root; // Responsive structure; the presentation adapter fills measured geometry.
	std::map<std::string, std::string> selected_pages;
	UiRect body;
	std::vector<UiPlacement> placements;
	std::vector<UiTab> pages;
	std::map<std::string, std::string> page_groups;
	std::map<std::string, UiRect> containers;
	std::vector<std::string> diagnostics;
	bool compact = false;
};
struct WidgetBinding
{
	const char *kind;
	const char *binding;
};
std::vector<UiComponentSpec> console_ui_components();
Json console_ui_header_template();
void verify_ui_condition(const Json &);
bool ui_condition_matches(const Json &, const UiAvailability &);
bool ui_widget_available(const Json &, const UiAvailability &);
Json expand_ui_groups(const Catalog &, const Json &node);
UiLayout compile_ui_layout(const Catalog &, const Json &console);
UiLayout available_ui_layout(UiLayout, const UiAvailability &);
UiLayout compile_ui_tree(const Catalog &, const Json &definition);
UiResolved resolve_ui_layout(const UiLayout &, UiRect available, float text_scale,
                             const std::map<std::string, std::string> &selected_pages = {});
UiResolved resolve_console_ui(const UiLayout &, float width, float height, float text_scale,
                              const std::map<std::string, std::string> &selected_pages = {},
                              bool multiple_consoles = false);
Json convert_console_layout(const Json &console);
std::vector<std::string> ui_theme_artwork_slots();
Json resolved_ui_theme(const Catalog &, const std::string &id = {}, const Json &overrides = Json::object());
Json public_ui_theme(const Catalog &);
void verify_ui_definitions(const Catalog &);
std::span<const WidgetBinding> widget_bindings();
} // namespace phoenix
