module;
#include <algorithm>
#include <string>
#include <tuple>
#include <vector>
module phoenix.ui.interaction;

namespace phoenix
{
bool same_semantic_controls(const std::vector<SemanticControl> &a, const std::vector<SemanticControl> &b)
{
	if (a.size() != b.size())
		return false;
	const auto fields = [](const SemanticControl &c)
	{
		return std::tie(c.id, c.label, c.placement.x, c.placement.y, c.placement.width, c.placement.height,
		                c.placement.focused, c.placement.enabled, c.editable, c.value, c.percentage, c.read_only, c.tab,
		                c.selected, c.toggle, c.pressed, c.numeric, c.minimum, c.maximum, c.step);
	};
	return std::equal(a.begin(), a.end(), b.begin(),
	                  [&](const auto &x, const auto &y)
	                  {
		                  return fields(x) == fields(y);
	                  });
}
} // namespace phoenix
