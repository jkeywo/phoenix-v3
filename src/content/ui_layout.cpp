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
UiNode compile_node(const Catalog &catalog, const Json &source, std::set<std::string> &ids,
                    std::set<std::string> groups, const std::string &prefix, unsigned depth, unsigned &count)
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
	if (source.contains("group"))
	{
		const auto group = source.at("group").get<std::string>();
		ui_require(catalog.ui_groups.contains(group) && groups.insert(group).second, node.id,
		           "unresolved or recursive reusable group " + group);
		auto definition = catalog.ui_groups.at(group);
		ui_require(definition.value("schema", 0) == 1, group, "expected UI schema 1");
		auto root = definition.at("root");
		if (source.contains("overrides"))
			root.merge_patch(source.at("overrides"));
		// The instance owns identity; reusable group children are namespaced.
		root["id"] = local;
		ids.erase(node.id);
		return compile_node(catalog, root, ids, groups, prefix, depth + 1, count);
	}
	ui_require(node.kind == "row" || node.kind == "column" || node.kind == "wrap" || node.kind == "tabs" ||
	               node.kind == "instruments" || node.kind == "component" || node.kind == "cutout",
	           node.id, "unsupported layout node kind");
	node.component = source.value("component", std::string{});
	node.style = source.value("style", Json::object());
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
			node.children.push_back(compile_node(catalog, child, ids, groups, node.id + ".", depth + 1, count));
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
		w = node.kind == "row" ? w + cw : std::max(w, cw);
		h = node.kind == "column" ? h + ch : std::max(h, ch);
	}
	const auto gaps = node.gap * scale * (node.children.size() - 1);
	if (node.kind == "row")
		w += gaps;
	if (node.kind == "column")
		h += gaps;
	if (node.kind == "tabs")
		h += 48 * scale;
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
	result.root = compile_node(catalog, definition.at("root"), ids, {}, {}, 0, count);
	std::set<std::string> full;
	targets(result.root, full);
	if (definition.contains("compact"))
	{
		ids.clear();
		result.compact = compile_node(catalog, definition.at("compact"), ids, {}, {}, 0, count);
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
	std::set<std::string> components;
	targets(result.root, components);
	std::set<std::string> allowed = {"gameplay",
	                                 "controls",
	                                 "readouts",
	                                 "messages",
	                                 "engineering",
	                                 "sensors",
	                                 "conversations",
	                                 "conversation-list",
	                                 "conversation-history",
	                                 "conversation-replies",
	                                 "navigation",
	                                 "manual-flight",
	                                 "conversation-header",
	                                 "conversation-compose",
	                                 "conversation-incoming"};
	for (const auto &widget : console.at("widgets"))
		allowed.insert(widget.at("id").get<std::string>());
	for (const auto &target : components)
		ui_require(allowed.contains(target), "console.layout", "unknown component " + target);
	return result;
}
UiResolved resolve_ui_layout(const UiLayout &layout, UiRect available, float text_scale,
                             const std::map<std::string, std::string> &selected_pages)
{
	ui_require(std::isfinite(text_scale) && text_scale > 0 && text_scale <= 4 && std::isfinite(available.width) &&
	               std::isfinite(available.height) && available.width > 0 && available.height > 0,
	           "layout.viewport", "expected positive finite bounds and scale");
	UiResolved result;
	const auto [mw, mh] = minimum_size(layout.root, text_scale);
	result.compact = available.width < mw || available.height < mh;
	UiNode automatic;
	const UiNode *root = &layout.root;
	if (result.compact)
	{
		if (layout.compact)
			root = &*layout.compact;
		else if (!root->children.empty() && root->kind != "tabs")
		{
			automatic = *root;
			automatic.kind = "tabs";
			root = &automatic;
		}
	}
	std::function<void(const UiNode &, UiRect, bool, const std::string &)> place;
	place = [&](const UiNode &node, UiRect area, bool visible, const std::string &page)
	{
		if (result.compact && (node.kind == "instruments" || node.kind == "cutout") &&
		    (area.width < minimum_size(node, text_scale).first || area.height < minimum_size(node, text_scale).second))
		{
			auto fallback = node;
			fallback.kind = "column";
			place(fallback, area, visible, page);
			return;
		}
		result.containers[node.id] = area;
		if (node.kind == "component")
		{
			const auto [w, h] = minimum_size(node, text_scale);
			if (visible && (area.width < w || area.height < h))
				result.diagnostics.push_back(node.id + ": scroll required to retain minimum control size");
			area.width = std::max(area.width, w);
			area.height = std::max(area.height, h);
			if (node.aspect > 0)
			{
				const float width = std::min(area.width, area.height * node.aspect), height = width / node.aspect;
				area.x += (area.width - width) / 2;
				area.y += (area.height - height) / 2;
				area.width = width;
				area.height = height;
			}
			result.placements.push_back({node.id, node.component, page, area, visible, node.style});
			return;
		}
		const auto pad = node.padding * text_scale, gap = node.gap * text_scale;
		area = {area.x + pad, area.y + pad, std::max(1.f, area.width - pad * 2), std::max(1.f, area.height - pad * 2)};
		if (node.kind == "tabs")
		{
			std::string chosen = node.children.front().id;
			if (selected_pages.contains(node.id))
				for (const auto &child : node.children)
					if (child.id == selected_pages.at(node.id))
						chosen = child.id;
			for (const auto &child : node.children)
			{
				if (visible)
				{
					result.pages.push_back({child.id, child.label});
					result.page_groups[child.id] = node.id;
				}
				place(child,
				      {area.x, area.y + 48 * text_scale, area.width, std::max(1.f, area.height - 48 * text_scale)},
				      visible && child.id == chosen, node.id);
			}
			return;
		}
		if (node.kind == "instruments" || node.kind == "cutout")
		{
			for (const auto &child : node.children)
			{
				auto r = child.rect;
				if (node.kind == "cutout" && !child.region.empty())
				{
					const auto x = node.circle_x * 1000, y = node.circle_y * 1000, radius = node.circle_radius * 1000;
					const auto corner = radius / std::sqrt(2.f);
					if (child.region == "centre")
						r = {x - radius, y - radius, radius * 2, radius * 2};
					else
					{
						const bool right = child.region.ends_with("right"), bottom = child.region.starts_with("bottom");
						r = {right ? x + corner : 0, bottom ? y + corner : 0, right ? 1000 - x - corner : x - corner,
						     bottom ? 1000 - y - corner : y - corner};
					}
				}
				place(child,
				      {area.x + area.width * r.x / 1000, area.y + area.height * r.y / 1000, area.width * r.width / 1000,
				       area.height * r.height / 1000},
				      visible, page);
			}
			return;
		}
		if (node.kind == "wrap")
		{
			float x = area.x, y = area.y, line = 0;
			for (const auto &child : node.children)
			{
				const auto [mw, mh] = minimum_size(child, text_scale);
				const float w = std::max(mw, child.preferred_width * text_scale),
				            h = std::max(mh, child.preferred_height * text_scale);
				if (x > area.x && x + w > area.x + area.width)
				{
					x = area.x;
					y += line + gap;
					line = 0;
				}
				place(child, {x, y, w, h}, visible, page);
				x += w + gap;
				line = std::max(line, h);
			}
			return;
		}
		const bool horizontal = node.kind == "row";
		float required = gap * (node.children.size() - 1), weight = 0;
		std::vector<float> sizes;
		for (const auto &child : node.children)
		{
			const auto [w, h] = minimum_size(child, text_scale);
			const auto size = std::max(horizontal ? w : h,
			                           (horizontal ? child.preferred_width : child.preferred_height) * text_scale);
			sizes.push_back(size);
			required += size;
			weight += child.stretch;
		}
		const auto spare = std::max(0.f, (horizontal ? area.width : area.height) - required);
		float position = horizontal ? area.x : area.y;
		for (std::size_t i = 0; i < node.children.size(); ++i)
		{
			const auto &child = node.children[i];
			const float size = sizes[i] + (weight > 0 ? spare * child.stretch / weight : 0);
			UiRect bounds =
			    horizontal ? UiRect{position, area.y, size, area.height} : UiRect{area.x, position, area.width, size};
			if (child.align != "stretch")
			{
				const auto [w, h] = minimum_size(child, text_scale);
				const float cross = std::max(
				    horizontal ? h : w, (horizontal ? child.preferred_height : child.preferred_width) * text_scale);
				const auto offset =
				    ((horizontal ? bounds.height : bounds.width) - cross) * (child.align == "center" ? .5f
				                                                             : child.align == "end"  ? 1.f
				                                                                                     : 0.f);
				if (horizontal)
				{
					bounds.y += std::max(0.f, offset);
					bounds.height = cross;
				}
				else
				{
					bounds.x += std::max(0.f, offset);
					bounds.width = cross;
				}
			}
			place(child, bounds, visible, page);
			position += size + gap;
		}
	};
	place(*root, available, true, {});
	return result;
}
Json convert_console_layout(const Json &console)
{
	Json instruments = {{"id", "instruments"}, {"label", "Instruments"}, {"kind", "instruments"},
	                    {"min_width", 640},    {"min_height", 480},      {"children", Json::array()}};
	for (const auto &widget : console.at("widgets"))
		instruments["children"].push_back({{"id", widget.at("id")},
		                                   {"label", widget.at("label")},
		                                   {"kind", "component"},
		                                   {"component", widget.at("id")},
		                                   {"rect",
		                                    {{"x", widget.at("x")},
		                                     {"y", widget.at("y")},
		                                     {"width", widget.at("width")},
		                                     {"height", widget.at("height")}}}});
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
Json resolved_ui_theme(const Catalog &catalog, const std::string &id, const Json &overrides)
{
	Json theme = {{"schema", 1},
	              {"tokens",
	               {{"background", "#08131b"},
	                {"panel", "#102027"},
	                {"text", "#fff0d4"},
	                {"muted", "#b6c2c8"},
	                {"accent", "#d6ac64"},
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
			(void)decode_asset_image(bytes);
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
			const auto image = decode_asset_image(catalog.assets.at(theme.at("assets").at(slot).get<std::string>()));
			ui_require(slices[1].get<int>() + slices[3].get<int>() < image.width &&
			               slices[0].get<int>() + slices[2].get<int>() < image.height,
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
		if (console.contains("layout"))
			(void)compile_ui_layout(catalog, console);
		if (console.contains("theme"))
			(void)resolved_ui_theme(catalog, console.at("theme").get<std::string>(),
			                        console.value("theme_overrides", Json::object()));
	}
}
} // namespace phoenix
