export module phoenix.platform.viewport;
export namespace phoenix
{
// Native windows and browser canvases expose separate keyboard and page focus.
bool window_has_focus();
bool drawing_surface_has_keyboard_focus();
// Browser CSS dimensions drive the backing surface; native windows own their size.
void synchronize_drawing_surface_size();
} // namespace phoenix
