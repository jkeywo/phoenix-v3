module;
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.content;

namespace phoenix
{
namespace
{
void ui_require(bool valid, const std::string &path, const std::string &reason)
{
	if (!valid)
		throw std::runtime_error(path + ": " + reason);
}
float ui_number(const Json &j, const char *key, float fallback, float minimum = 0, float maximum = 16384)
{
	if (!j.contains(key))
		return fallback;
	ui_require(j.at(key).is_number(), key, "expected number");
	const auto value = j.at(key).get<float>();
	ui_require(std::isfinite(value) && value >= minimum && value <= maximum, key, "outside supported range");
	return value;
}
Json expand_node(const Catalog &catalog, Json source, std::set<std::string> groups, unsigned depth, unsigned &count)
{
	ui_require(depth <= 24 && ++count <= 512, "layout", "exceeds 24 levels or 512 nodes");
	ui_require(source.is_object(), "layout", "expected node object");
	if (source.contains("group"))
	{
		const auto group = source.at("group").get<std::string>();
		ui_require(catalog.ui_groups.contains(group) && groups.insert(group).second, "layout.group",
		           "unresolved or recursive reusable group " + group);
		const auto &definition = catalog.ui_groups.at(group);
		ui_require(definition.value("schema", 0) == 1, group, "expected UI schema 1");
		auto root = definition.at("root");
		if (source.contains("overrides"))
			root.merge_patch(source.at("overrides"));
		for (const char *field : {"label", "rect", "min_width", "min_height", "width", "height", "gap", "padding",
		                          "stretch", "aspect", "align", "region", "circle", "style", "when"})
			if (source.contains(field))
				root[field] = source.at(field);
		root["id"] = source.at("id");
		return expand_node(catalog, std::move(root), std::move(groups), depth + 1, count);
	}
	if (source.contains("children"))
		for (auto &child : source["children"])
			child = expand_node(catalog, std::move(child), groups, depth + 1, count);
	return source;
}
UiNode compile_node(const Catalog &catalog, const Json &source, std::set<std::string> &ids, const std::string &prefix,
                    unsigned depth, unsigned &count)
{
	ui_require(depth <= 24 && ++count <= 512, "layout", "exceeds 24 levels or 512 nodes");
	ui_require(source.is_object(), "layout", "expected node object");
	const auto local = source.at("id").get<std::string>();
	ui_require(!local.empty() && local.size() <= 128 &&
	               local.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
	                   std::string::npos,
	           "layout.id", "expected stable identifier");
	UiNode node;
	node.id = prefix + local;
	ui_require(ids.insert(node.id).second, node.id, "duplicate node identity");
	node.kind = source.value("kind", std::string("column"));
	node.label = source.value("label", local);
	ui_require(node.kind == "row" || node.kind == "column" || node.kind == "wrap" || node.kind == "tabs" ||
	               node.kind == "instruments" || node.kind == "component" || node.kind == "cutout",
	           node.id, "unsupported layout node kind");
	node.component = source.value("component", std::string{});
	node.style = source.value("style", Json::object());
	if (source.contains("when"))
	{
		verify_ui_condition(source.at("when"));
		node.conditions.push_back(source.at("when"));
	}
	if (!node.style.empty())
		(void)resolved_ui_theme(catalog, {}, Json{{"tokens", node.style}});
	node.minimum_width = ui_number(source, "min_width", 44, 1);
	node.minimum_height = ui_number(source, "min_height", 44, 1);
	node.preferred_width = ui_number(source, "width", 0);
	node.preferred_height = ui_number(source, "height", 0);
	node.gap = ui_number(source, "gap", 12, 0, 512);
	node.padding = ui_number(source, "padding", 8, 0, 512);
	node.stretch = ui_number(source, "stretch", 1, 0, 1000);
	node.aspect = ui_number(source, "aspect", 0, 0, 100);
	node.align = source.value("align", std::string("stretch"));
	node.region = source.value("region", std::string{});
	ui_require(node.region.empty() || node.region == "centre" || node.region == "top-left" ||
	               node.region == "top-right" || node.region == "bottom-left" || node.region == "bottom-right",
	           node.id, "unknown cutout region");
	if (source.contains("circle"))
	{
		const auto &circle = source.at("circle");
		node.circle_x = ui_number(circle, "x", .5f, 0, 1);
		node.circle_y = ui_number(circle, "y", .5f, 0, 1);
		node.circle_radius = ui_number(circle, "radius", .35f, .01f, .5f);
		ui_require(node.circle_x >= node.circle_radius && node.circle_y >= node.circle_radius &&
		               node.circle_x + node.circle_radius <= 1 && node.circle_y + node.circle_radius <= 1,
		           node.id, "circle exceeds group bounds");
	}
	ui_require(node.align == "stretch" || node.align == "start" || node.align == "center" || node.align == "end",
	           node.id, "unsupported alignment");
	if (source.contains("rect"))
	{
		const auto &r = source.at("rect");
		node.rect = {ui_number(r, "x", 0, 0, 1000), ui_number(r, "y", 0, 0, 1000), ui_number(r, "width", 1000, 1, 1000),
		             ui_number(r, "height", 1000, 1, 1000)};
		ui_require(node.rect.x + node.rect.width <= 1000 && node.rect.y + node.rect.height <= 1000, node.id,
		           "instrument rectangle exceeds 1000 by 1000 group");
	}
	if (node.kind == "component")
		ui_require(!node.component.empty() && !source.contains("children"), node.id,
		           "component needs one target and no children");
	else
	{
		ui_require(node.component.empty(), node.id, "only component nodes may target controls");
		ui_require(source.contains("children") && source.at("children").is_array() && !source.at("children").empty(),
		           node.id, "container needs children");
		for (const auto &child : source.at("children"))
			node.children.push_back(compile_node(catalog, child, ids, node.id + ".", depth + 1, count));
	}
	return node;
}
std::pair<float, float> minimum_size(const UiNode &node, float scale)
{
	float width = node.minimum_width * scale, height = node.minimum_height * scale;
	if (node.children.empty())
		return {width, height};
	float w = 0, h = 0;
	for (const auto &child : node.children)
	{
		const auto [cw, ch] = minimum_size(child, scale);
		if (node.kind == "instruments")
		{
			w = std::max(w, cw * 1000 / child.rect.width);
			h = std::max(h, ch * 1000 / child.rect.height);
			continue;
		}
		w = node.kind == "row" ? w + cw : std::max(w, cw);
		h = node.kind == "column" ? h + ch : std::max(h, ch);
	}
	const auto gaps = node.gap * scale * (node.children.size() - 1);
	if (node.kind == "row")
		w += gaps;
	if (node.kind == "column")
		h += gaps;
	return {std::max(width, w + node.padding * 2 * scale), std::max(height, h + node.padding * 2 * scale)};
}
void targets(const UiNode &node, std::set<std::string> &result)
{
	if (!node.component.empty())
		ui_require(result.insert(node.component).second, node.id,
		           "component appears more than once: " + node.component);
	for (const auto &child : node.children)
		targets(child, result);
}
} // namespace

Json console_ui_header_template()
{
	return Json{{"schema", 1},
	            {"root",
	             {{"id", "header"},
	              {"kind", "row"},
	              {"min_height", 56},
	              {"gap", 8},
	              {"padding", 4},
	              {"children", Json::array({{{"id", "settings"},
	                                         {"kind", "component"},
	                                         {"component", "settings"},
	                                         {"min_width", 56},
	                                         {"width", 56},
	                                         {"stretch", 0}},
	                                        {{"id", "name"},
	                                         {"kind", "component"},
	                                         {"component", "console-name"},
	                                         {"min_width", 120},
	                                         {"width", 160},
	                                         {"stretch", 0}},
	                                        {{"id", "tabs"},
	                                         {"kind", "component"},
	                                         {"component", "tabs"},
	                                         {"min_width", 200},
	                                         {"stretch", 1}}})}}}};
}
std::vector<UiComponentSpec> console_ui_components()
{
	return {{"settings", "Settings", {}},
	        {"console-name", "Station name", {}},
	        {"tabs", "Station tabs", {}},
	        {"weapons", "Weapon toggles", "weapons"},
	        {"target-info", "Target information", {}},
	        {"objectives", "Objectives", {}},
	        {"viewscreen-controls", "Viewscreen cameras", "camera"},
	        {"alert-controls", "Alert condition", "alert"},
	        {"shield-display", "Shield diagram", "shields"},
	        {"ftl-controls", "FTL controls", "ftl"},
	        {"compartment-health", "Compartment health", {}},
	        {"team-roster", "Repair and Medical teams", "teams"},
	        {"gameplay", "Operating panel", {}},
	        {"controls", "Controls", {}},
	        {"readouts", "Readouts", {}},
	        {"messages", "Messages", {}},
	        {"engineering", "Engineering cards", {}},
	        {"sensors", "Sensors", {}},
	        {"conversations", "Conversations", {}},
	        {"conversation-list", "Conversation list", {}},
	        {"conversation-header", "Conversation header", {}},
	        {"conversation-history", "Conversation history", {}},
	        {"conversation-replies", "Conversation replies", {}},
	        {"conversation-compose", "Conversation composer", {}},
	        {"conversation-incoming", "Incoming conversations", {}},
	        {"navigation", "Navigation", {}},
	        {"manual-flight", "Manual flight", {}},
	        {"power", "Power", "power"},
	        {"repair", "Repair", "repair"},
	        {"operations", "Operations", "operations"},
	        {"docking", "Docking", "docking"},
	        {"craft", "Flight operations", "craft"},
	        {"personnel", "Personnel", "personnel"},
	        {"away", "Away missions", "away"}};
}
bool UiAvailability::operator==(const UiAvailability &other) const
{
	auto same = [](const auto &a, const auto &b)
	{
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
	};
	return profile == other.profile && stars == other.stars && same(actions, other.actions) &&
	       same(controllers, other.controllers) && same(bindings, other.bindings) && same(systems, other.systems) &&
	       same(features, other.features);
}
void verify_ui_condition(const Json &condition)
{
	ui_require(condition.is_object(), "UI.when", "expected object");
	for (const auto &[key, value] : condition.items())
	{
		if (key == "min_stars" || key == "max_stars")
			ui_require(value.is_number_integer() && value >= 1 && value <= 3, "UI.when." + key,
			           "expected integer from 1 to 3");
		else
		{
			ui_require(key == "profiles" || key == "actions" || key == "controllers" || key == "bindings" ||
			               key == "systems" || key == "features",
			           "UI.when", "unknown condition " + key);
			ui_require(value.is_array() && !value.empty(), "UI.when." + key, "expected nonempty identifier array");
			for (const auto &id : value)
				ui_require(id.is_string() && !id.get<std::string>().empty(), "UI.when." + key, "expected identifier");
		}
	}
	ui_require(condition.value("min_stars", 1) <= condition.value("max_stars", 3), "UI.when",
	           "min_stars exceeds max_stars");
}
bool ui_condition_matches(const Json &condition, const UiAvailability &available)
{
	if ((condition.contains("min_stars") || condition.contains("max_stars")) &&
	    (available.stars < condition.value("min_stars", 1) || available.stars > condition.value("max_stars", 3)))
		return false;
	for (const auto &[key, value] : condition.items())
	{
		if (key == "min_stars" || key == "max_stars")
			continue;
		const auto *ids = key == "actions"       ? &available.actions
		                  : key == "controllers" ? &available.controllers
		                  : key == "bindings"    ? &available.bindings
		                  : key == "systems"     ? &available.systems
		                                         : &available.features;
		if (!std::any_of(value.begin(), value.end(),
		                 [&](const Json &id)
		                 {
			                 return key == "profiles" ? id == available.profile : ids->contains(id.get<std::string>());
		                 }))
			return false;
	}
	return true;
}
bool ui_widget_available(const Json &widget, const UiAvailability &available)
{
	if (widget.contains("when") && !ui_condition_matches(widget.at("when"), available))
		return false;
	const auto kind = widget.value("kind", std::string{});
	// Informational widgets (radar, conversations, etc.) can remain useful under AI control.
	if ((kind == "throttle" || kind == "heading") && widget.contains("controller") && widget.contains("channel"))
		return available.bindings.contains("controller/" + widget.at("controller").get<std::string>() + "/" +
		                                   widget.at("channel").get<std::string>());
	return true;
}
UiLayout available_ui_layout(UiLayout layout, const UiAvailability &available)
{
	std::function<bool(UiNode &)> keep = [&](UiNode &node)
	{
		if (!std::all_of(node.conditions.begin(), node.conditions.end(),
		                 [&](const Json &condition)
		                 {
			                 return ui_condition_matches(condition, available);
		                 }))
			return false;
		std::erase_if(node.children,
		              [&](UiNode &child)
		              {
			              return !keep(child);
		              });
		return node.kind == "component" || !node.children.empty();
	};
	auto filter = [&](UiNode &root)
	{
		if (!keep(root))
		{
			root.kind = "column";
			root.component.clear();
			root.children.clear();
		}
	};
	filter(layout.root);
	if (layout.compact)
		filter(*layout.compact);
	return layout;
}

Json expand_ui_groups(const Catalog &catalog, const Json &source)
{
	unsigned count = 0;
	return expand_node(catalog, source, {}, 0, count);
}
bool UiTabs::update(std::vector<UiTab> tabs)
{
	std::set<std::string> unique;
	for (const auto &tab : tabs)
		ui_require(!tab.id.empty() && unique.insert(tab.id).second, "tabs", "duplicate or empty identity");
	const auto previous = selected_;
	auto old = std::find_if(items_.begin(), items_.end(),
	                        [&](const UiTab &tab)
	                        {
		                        return tab.id == previous;
	                        });
	const auto index = old == items_.end() ? 0u : static_cast<unsigned>(old - items_.begin());
	items_ = std::move(tabs);
	if (!std::any_of(items_.begin(), items_.end(),
	                 [&](const UiTab &tab)
	                 {
		                 return tab.id == selected_ && tab.enabled;
	                 }))
	{
		selected_.clear();
		for (unsigned i = 0; i < items_.size(); ++i)
		{
			const auto &tab = items_[(index + i) % items_.size()];
			if (tab.enabled)
			{
				selected_ = tab.id;
				break;
			}
		}
	}
	return selected_ != previous;
}
bool UiTabs::select(const std::string &id)
{
	if (id == selected_ || !std::any_of(items_.begin(), items_.end(),
	                                    [&](const UiTab &tab)
	                                    {
		                                    return tab.id == id && tab.enabled;
	                                    }))
		return false;
	selected_ = id;
	return true;
}
UiLayout compile_ui_tree(const Catalog &catalog, const Json &definition)
{
	ui_require(definition.value("schema", 0) == 1, "layout", "expected UI schema 1");
	UiLayout result;
	std::set<std::string> ids;
	unsigned count = 0;
	result.root = compile_node(catalog, expand_ui_groups(catalog, definition.at("root")), ids, {}, 0, count);
	std::set<std::string> full;
	targets(result.root, full);
	if (definition.contains("compact"))
	{
		ids.clear();
		result.compact = compile_node(catalog, expand_ui_groups(catalog, definition.at("compact")), ids, {}, 0, count);
		std::set<std::string> compact;
		targets(*result.compact, compact);
		ui_require(full == compact, "layout.compact", "must preserve all component targets exactly once");
	}
	return result;
}
UiLayout compile_ui_layout(const Catalog &catalog, const Json &console)
{
	auto source = console.at("layout");
	if (source.is_string())
	{
		const auto id = source.get<std::string>();
		ui_require(catalog.layouts.contains(id), "console.layout", "unresolved layout " + id);
		source = catalog.layouts.at(id);
	}
	if (console.contains("layout_overrides"))
		source.merge_patch(console.at("layout_overrides"));
	auto result = compile_ui_tree(catalog, source);
	auto header = console.value("header", console_ui_header_template());
	if (header.is_string())
	{
		ui_require(catalog.layouts.contains(header.get<std::string>()), "console.header", "unresolved header layout");
		header = catalog.layouts.at(header.get<std::string>());
	}
	if (console.contains("header_overrides"))
		header.merge_patch(console.at("header_overrides"));
	result.header = compile_ui_tree(catalog, header).root;
	std::set<std::string> header_targets;
	targets(*result.header, header_targets);
	for (const auto &target : header_targets)
		ui_require(target == "settings" || target == "console-name" || target == "tabs", "console.header",
		           "unsupported header component " + target);
	ui_require(header_targets == std::set<std::string>{"settings", "console-name", "tabs"}, "console.header",
	           "requires Settings, console name and tabs");

	std::set<std::string> components;
	targets(result.root, components);
	std::set<std::string> allowed;
	for (const auto &component : console_ui_components())
	{
		allowed.insert(component.id);
		if (component.feature.empty())
			continue;
		std::function<void(UiNode &)> require_capability = [&](UiNode &node)
		{
			if (node.component == component.id)
				node.conditions.push_back(Json{{"features", {component.feature}}});
			for (auto &child : node.children)
				require_capability(child);
		};
		require_capability(result.root);
		if (result.compact)
			require_capability(*result.compact);
	}

	for (const auto &widget : console.at("widgets"))
	{
		const auto id = widget.at("id").get<std::string>();
		allowed.insert(id);
		if (widget.contains("when"))
			verify_ui_condition(widget.at("when"));
		std::function<void(UiNode &)> condition = [&](UiNode &node)
		{
			if (node.component == id || (widget.at("kind") == "navigation" && node.component == "navigation") ||
			    (widget.at("kind") == "conversations" &&
			     (node.component == "conversations" || node.component.starts_with("conversation-"))))
			{
				if (widget.contains("when"))
					node.conditions.push_back(widget.at("when"));
				const auto kind = widget.at("kind").get<std::string>();
				if ((kind == "throttle" || kind == "heading") && widget.contains("controller") &&
				    widget.contains("channel"))
					node.conditions.push_back(Json{{"bindings",
					                                {"controller/" + widget.at("controller").get<std::string>() + "/" +
					                                 widget.at("channel").get<std::string>()}}});
			}
			for (auto &child : node.children)
				condition(child);
		};
		condition(result.root);
		if (result.compact)
			condition(*result.compact);
	}
	for (const auto &target : components)
		ui_require(allowed.contains(target) && !header_targets.contains(target), "console.layout",
		           "unknown or header-only component " + target);
	return result;
}
UiResolved resolve_ui_layout(const UiLayout &layout, UiRect available, float text_scale,
                             const std::map<std::string, std::string> &selected_pages)
{
	ui_require(std::isfinite(text_scale) && text_scale > 0 && text_scale <= 4 && std::isfinite(available.width) &&
	               std::isfinite(available.height) && available.width > 0 && available.height > 0,
	           "layout.viewport", "expected positive finite bounds and scale");
	UiResolved result;
	result.body = available;
	const auto [mw, mh] = minimum_size(layout.root, text_scale);
	result.compact = available.width < mw || available.height < mh;
	result.root = result.compact && layout.compact ? *layout.compact : layout.root;
	if (result.compact && !layout.compact && !result.root.children.empty() && result.root.kind != "tabs")
	{
		result.root.kind = "tabs";
		result.root.minimum_width = result.root.minimum_height = 44;
	}
	// Only choose responsive structure and page visibility here. RmlUi owns container geometry.
	std::function<void(UiNode &, bool, const std::string &)> visit;
	visit = [&](UiNode &node, bool visible, const std::string &page)
	{
		if (result.compact && (node.kind == "instruments" || node.kind == "cutout"))
		{
			node.kind = "column";
			node.minimum_width = node.minimum_height = 44;
		}
		if (node.kind == "component")
			result.placements.push_back({node.id, node.component, page, {}, visible, node.style});
		if (node.kind == "tabs" && !node.children.empty())
		{
			auto chosen = node.children.front().id;
			if (selected_pages.contains(node.id))
				for (const auto &child : node.children)
					if (child.id == selected_pages.at(node.id))
						chosen = child.id;
			result.selected_pages[node.id] = chosen;
			for (auto &child : node.children)
			{
				if (visible)
				{
					result.pages.push_back({child.id, child.label});
					result.page_groups[child.id] = node.id;
				}
				visit(child, visible && child.id == chosen, node.id);
			}
		}
		else
			for (auto &child : node.children)
				visit(child, visible, page);
	};
	visit(result.root, true, {});
	return result;
}
UiResolved resolve_console_ui(const UiLayout &layout, float width, float height, float text_scale,
                              const std::map<std::string, std::string> &pages, bool multiple_consoles)
{
	float header = layout.header ? std::max(56.f, layout.header->minimum_height) * text_scale
	                             : (multiple_consoles ? 112.f : 64.f) * text_scale;
	auto result = resolve_ui_layout(layout, {0, 0, width, std::max(100.f, height - header)}, text_scale, pages);
	if (!layout.header && !result.pages.empty())
	{
		header += 48 * text_scale;
		result = resolve_ui_layout(layout, {0, 0, width, std::max(100.f, height - header)}, text_scale, pages);
	}
	result.body.y = header;
	return result;
}
Json convert_console_layout(const Json &console)
{
	Json instruments = {{"id", "instruments"}, {"label", "Instruments"}, {"kind", "instruments"},
	                    {"min_width", 640},    {"min_height", 480},      {"children", Json::array()}};
	const bool helm = std::any_of(console.at("widgets").begin(), console.at("widgets").end(),
	                              [](const auto &w)
	                              {
		                              return w.at("kind") == "throttle";
	                              });
	for (const auto &widget : console.at("widgets"))
	{
		const auto kind = widget.at("kind").get<std::string>();
		Json rect{{"x", widget.at("x")},
		          {"y", widget.at("y")},
		          {"width", widget.at("width")},
		          {"height", widget.at("height")}};
		if (helm && std::count_if(console.at("widgets").begin(), console.at("widgets").end(),
		                          [](const auto &w)
		                          {
			                          return w.at("kind") == "throttle";
		                          }) == 1)
		{
			if (kind == "radar")
				rect = {{"x", 240}, {"y", 0}, {"width", 600}, {"height", 1000}};
			if (kind == "throttle")
				rect = {{"x", 850}, {"y", 100}, {"width", 150}, {"height", 800}};
			if (kind == "heading")
				rect = {{"x", 0}, {"y", 850}, {"width", 220}, {"height", 150}};
			if (kind == "camera_mode")
				rect = {{"x", 0}, {"y", 80}, {"width", 220}, {"height", 220}};
			if (kind == "camera_direction")
				rect = {{"x", 0}, {"y", 300}, {"width", 220}, {"height", 450}};
		}
		Json node{{"id", widget.at("id")},
		          {"label", widget.at("label")},
		          {"kind", "component"},
		          {"component", kind == "navigation" ? Json("navigation") : widget.at("id")},
		          {"rect", rect},
		          {"min_width", kind == "radar"      ? 280
		                        : kind == "throttle" ? 72
		                                             : 160},
		          {"min_height", kind == "radar"      ? 280
		                         : kind == "throttle" ? 240
		                                              : 72}};
		if (kind == "radar")
			node["aspect"] = 1;
		instruments["children"].push_back(node);
	}
	if (helm)
		instruments["children"].push_back({{"id", "manual"},
		                                   {"label", "Manual flight"},
		                                   {"kind", "component"},
		                                   {"component", "manual-flight"},
		                                   {"rect", {{"x", 0}, {"y", 0}, {"width", 1000}, {"height", 1000}}}});
	Json children = Json::array();
	if (!instruments.at("children").empty())
		children.push_back(instruments);
	children.push_back({{"id", "operations"},
	                    {"label", "Operations"},
	                    {"kind", "component"},
	                    {"component", "gameplay"},
	                    {"min_width", 280},
	                    {"min_height", 320}});
	return {{"schema", 1}, {"root", {{"id", "console"}, {"kind", "row"}, {"children", children}}}};
}
std::vector<std::string> ui_theme_artwork_slots()
{
	return {"background",     "panel",           "button",          "button-hover",
	        "button-pressed", "button-selected", "button-disabled", "tab",
	        "tab-selected",   "field",           "slider-track",    "slider-thumb",
	        "progress-track", "progress-fill",   "scrollbar-track", "scrollbar-thumb"};
}
Json resolved_ui_theme(const Catalog &catalog, const std::string &id, const Json &overrides)
{
	Json theme = {{"schema", 1},
	              {"tokens",
	               {{"background", "#08131b"},
	                {"panel", "#102027"},
	                {"text", "#fff0d4"},
	                {"muted", "#b6c2c8"},
	                {"accent", "#d6ac64"},
	                {"positive", "#88d69b"},
	                {"negative", "#ee8d83"},
	                {"hover", "#ffd5a5"},
	                {"pressed", "#d39a64"},
	                {"focus", "#ffe4b0"},
	                {"disabled", "#64717b"},
	                {"error", "#f08070"},
	                {"border", "#988b71"},
	                {"font_family", "LatoLatin"},
	                {"font_size", 22},
	                {"radius", 4},
	                {"border_width", 1},
	                {"gap", 12}}},
	              {"assets", Json::object()}};
	if (!catalog.ui_theme.empty())
	{
		ui_require(catalog.themes.contains(catalog.ui_theme), "theme", "unresolved pack theme " + catalog.ui_theme);
		theme.merge_patch(catalog.themes.at(catalog.ui_theme));
	}
	if (!id.empty())
	{
		ui_require(catalog.themes.contains(id), "theme", "unresolved theme " + id);
		theme.merge_patch(catalog.themes.at(id));
	}
	theme.merge_patch(overrides);
	ui_require(theme.value("schema", 0) == 1 && theme.at("tokens").is_object() && theme.at("assets").is_object(),
	           "theme", "expected schema 1 with tokens and assets");
	for (const auto &[key, value] : theme.at("tokens").items())
	{
		static const std::set<std::string> supported = {
		    "background",  "panel",     "text",   "muted",        "accent", "focus",    "disabled", "error", "border",
		    "font_family", "font_size", "radius", "border_width", "gap",    "positive", "negative", "hover", "pressed"};
		ui_require(supported.contains(key), "theme.tokens." + key, "unsupported visual property");
		if (key == "font_family")
		{
			const auto family = value.get<std::string>();
			ui_require(!family.empty() && family.size() <= 256 &&
			               family.find_first_of(";{}<>\"\\:()[]@\r\n") == std::string::npos,
			           "theme.font_family", "invalid font family");
		}
		else if (key == "font_size" || key == "radius" || key == "border_width" || key == "gap")
			(void)ui_number(theme.at("tokens"), key.c_str(), 0, key == "font_size" ? 12.f : 0.f, 128);
		else
		{
			const auto colour = value.get<std::string>();
			ui_require((colour.size() == 7 || colour.size() == 9) && colour[0] == '#' &&
			               colour.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos,
			           "theme." + key, "expected #RRGGBB or #RRGGBBAA");
		}
	}
	std::map<std::string, std::pair<int, int>> image_sizes;
	auto image_size = [&](const std::string &reference)
	{
		if (!image_sizes.contains(reference))
		{
			const auto image = decode_asset_image(catalog.assets.at(reference));
			image_sizes[reference] = {image.width, image.height};
		}
		return image_sizes.at(reference);
	};
	for (const auto &[key, value] : theme.at("assets").items())
	{
		ui_require(value.is_string() && catalog.assets.contains(value.get<std::string>()), "theme.assets." + key,
		           "unresolved asset");
		const auto ref = value.get<std::string>();
		const auto &bytes = catalog.assets.at(ref);
		if (key == "font" || key.starts_with("fallback-"))
			ui_require((ref.ends_with(".ttf") || ref.ends_with(".otf")) && bytes.size() >= 12, "theme.assets." + key,
			           "expected font asset");
		else
			(void)image_size(ref);
	}
	if (theme.contains("asset_scale"))
	{
		ui_require(theme.at("asset_scale").is_object(), "theme.asset_scale", "expected slot object");
		for (const auto &[slot, value] : theme.at("asset_scale").items())
		{
			ui_require(theme.at("assets").contains(slot), "theme.asset_scale." + slot, "requires image asset");
			(void)ui_number(theme.at("asset_scale"), slot.c_str(), 1, .001f, 8);
		}
	}
	if (theme.contains("slices"))
	{
		ui_require(theme.at("slices").is_object(), "theme.slices", "expected slot object");
		for (const auto &[slot, slices] : theme.at("slices").items())
		{
			ui_require(theme.at("assets").contains(slot) && slices.is_array() && slices.size() == 4,
			           "theme.slices." + slot, "requires image and four slice insets");
			for (const auto &v : slices)
				ui_require(v.is_number_integer() && v >= 0 && v <= 16384, "theme.slices",
				           "expected nonnegative pixel insets");
			const auto image = image_size(theme.at("assets").at(slot).get<std::string>());
			ui_require(slices[1].get<int>() + slices[3].get<int>() < image.first &&
			               slices[0].get<int>() + slices[2].get<int>() < image.second,
			           "theme.slices." + slot, "insets exceed image bounds");
		}
	}
	return theme;
}
Json public_ui_theme(const Catalog &catalog)
{
	auto theme = resolved_ui_theme(catalog);
	static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (const auto &[slot, value] : theme.at("assets").items())
	{
		// The pre-launch browser shell renders only these three artwork roles.
		// Runtime-only control skins remain in the content pack, not duplicated into launcher metadata.
		if (slot != "background" && slot != "panel" && slot != "button" && slot != "font" &&
		    !slot.starts_with("fallback-"))
			continue;
		const auto ref = value.get<std::string>();
		const auto bytes = ref.ends_with(".ttf") || ref.ends_with(".otf")
		                       ? catalog.assets.at(ref)
		                       : source_asset_image(decode_asset_image(catalog.assets.at(ref)));
		std::string encoded;
		for (std::size_t i = 0; i < bytes.size(); i += 3)
		{
			const unsigned a = bytes[i], b = i + 1 < bytes.size() ? bytes[i + 1] : 0,
			               c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
			const auto bits = (a << 16) | (b << 8) | c;
			encoded += alphabet[(bits >> 18) & 63];
			encoded += alphabet[(bits >> 12) & 63];
			encoded += i + 1 < bytes.size() ? alphabet[(bits >> 6) & 63] : '=';
			encoded += i + 2 < bytes.size() ? alphabet[bits & 63] : '=';
		}

		theme["asset_data"][slot] = std::string("data:") +
		                            (ref.ends_with(".ttf") || ref.ends_with(".otf") ? "font/ttf" : "image/png") +
		                            ";base64," + encoded;
	}
	theme.erase("assets");
	theme.erase("extends");
	return theme;
}
void verify_ui_definitions(const Catalog &catalog)
{
	(void)resolved_ui_theme(catalog);
	for (const auto &[id, definition] : catalog.layouts)
		(void)compile_ui_tree(catalog, definition);
	for (const auto &[id, definition] : catalog.ui_groups)
		(void)compile_ui_tree(catalog, definition);
	for (const auto &[id, definition] : catalog.themes)
		(void)resolved_ui_theme(catalog, id);
	for (const auto &[id, console] : catalog.consoles)
	{
		if (console.contains("tab"))
			ui_require(console.at("tab").is_boolean(), id + ".tab", "expected boolean");
		if (console.contains("when"))
			verify_ui_condition(console.at("when"));
		for (const auto &widget : console.at("widgets"))
			if (widget.contains("when"))
				verify_ui_condition(widget.at("when"));
		if (console.contains("compact"))
			for (const auto &widget : console.at("compact").at("widgets"))
				if (widget.contains("when"))
					verify_ui_condition(widget.at("when"));
		if (console.contains("layout"))
			(void)compile_ui_layout(catalog, console);
		if (console.contains("hud_layout"))
		{
			auto hud = console;
			hud["layout"] = console.at("hud_layout");
			hud.erase("layout_overrides");
			if (console.contains("hud_layout_overrides"))
				hud["layout_overrides"] = console.at("hud_layout_overrides");
			if (console.contains("compact"))
				hud["widgets"] = console.at("compact").at("widgets");
			(void)compile_ui_layout(catalog, hud);
		}
		if (console.contains("theme") || console.contains("theme_overrides"))
			(void)resolved_ui_theme(catalog, console.value("theme", std::string{}),
			                        console.value("theme_overrides", Json::object()));
	}
}
} // namespace phoenix
