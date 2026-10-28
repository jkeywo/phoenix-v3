module;
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(bool, phoenix_canvas_has_focus, (), { return document.activeElement === Module.canvas; });
EM_JS(bool, phoenix_document_has_focus, (), { return document.hasFocus(); });
EM_JS(int, phoenix_canvas_width, (), { return Math.max(1,Module.canvas.clientWidth); });
EM_JS(int, phoenix_canvas_height, (), { return Math.max(1,Module.canvas.clientHeight); });
// clang-format on
#endif
module phoenix.platform.viewport;
namespace phoenix
{
bool window_has_focus()
{
#ifdef __EMSCRIPTEN__
	return phoenix_document_has_focus();
#else
	return IsWindowFocused();
#endif
}
bool drawing_surface_has_keyboard_focus()
{
#ifdef __EMSCRIPTEN__
	return phoenix_canvas_has_focus();
#else
	return IsWindowFocused();
#endif
}
void synchronize_drawing_surface_size()
{
#ifdef __EMSCRIPTEN__
	const int width = phoenix_canvas_width(), height = phoenix_canvas_height();
	if (width != GetScreenWidth() || height != GetScreenHeight())
		SetWindowSize(width, height);
#endif
}
} // namespace phoenix
