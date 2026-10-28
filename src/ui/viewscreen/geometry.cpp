module;
#include <algorithm>
#include <raylib.h>
module phoenix.ui.viewscreen;

namespace phoenix
{
ViewscreenChrome viewscreen_chrome(Rectangle area)
{
	// Keep chrome at the same size when a sidebar narrows the scene; guard very narrow views.
	const auto scale = std::max(.25f, std::min(area.width / 960.f, area.height / 1080.f));
	const Rectangle frame{area.x + 24 * scale, area.y + area.height - 144 * scale, 360 * scale, 144 * scale};
	return {
	    scale, 112 * scale, frame, {frame.x + 16 * scale, frame.y + 24 * scale, frame.width - 56 * scale, 80 * scale}};
}
} // namespace phoenix
