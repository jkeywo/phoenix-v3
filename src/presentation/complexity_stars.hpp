#pragma once
namespace phoenix
{
inline std::string complexity_star_styles()
{
	return ".complexity-star { background:#263b49; border:2dp #668493 solid; }"
	       ".complexity-star.selected { border-color:#e9bf68; }"
	       ".complexity-star:hover { background:#3c596b; }"
	       ".complexity-star:focus { background:#3c596b; border-color:#ffffff; }";
}
inline int complexity_stars(const std::map<std::string, int> &levels, const std::string &profile)
{
	const auto found = levels.find(profile);
	return found == levels.end() ? 0 : found->second;
}
inline std::string complexity_star_image(int level, int selected, const std::string &path)
{
	return "<img style=\"width:32dp;height:32dp;pointer-events:none;\" src=\"" + path +
	       (level <= selected ? "star-full.png" : "star-empty.png") + "\"/>";
}
inline std::string complexity_star_button(std::string button, int level, int selected, const std::string &path)
{
	const auto begin = button.find('>');
	button.replace(begin + 1, button.rfind("</button>") - begin - 1, complexity_star_image(level, selected, path));
	button.insert(begin,
	              " style=\"display:inline-block;width:48dp;height:48dp;min-width:48dp;padding:6dp;margin:3dp;\"");
	const auto classes = button.find("class=\"");
	const auto name = std::string("complexity-star") + (level == selected ? " selected" : "");
	if (classes != std::string::npos)
		button.insert(classes + 7, name + " ");
	else
		button.insert(button.find('>'), " class=\"" + name + "\"");
	return button;
}
inline std::string complexity_star_readout(int selected, const std::string &path)
{
	if (!selected)
		return {};
	std::string result = "<div style=\"display:inline-block;background:#101b23;padding:6dp;\">";
	for (int level = 1; level <= 3; ++level)
		result += complexity_star_image(level, selected, path);
	return result + "</div>";
}
} // namespace phoenix
