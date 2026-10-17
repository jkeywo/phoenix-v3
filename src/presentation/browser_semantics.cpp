module;
#include <emscripten.h>
#include <deque>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module phoenix.presentation;
import phoenix.navigation;

namespace
{
std::deque<std::string> actions;
std::deque<std::string> focuses;
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_semantic_action(const char *id)
{
	if (actions.size() < 64)
		actions.emplace_back(id);
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_semantic_focus(const char *id)
{
	if (focuses.size() < 64)
		focuses.emplace_back(id);
}
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, begin_semantics, (), {
    const root = document.getElementById('semantics');
    for (const child of root.children) if (child.dataset.action) child.dataset.stale = 'true';
});
EM_JS(void, append_semantic_button, (const char* id, const char* label, float x, float y, float width, float height, bool focused, bool enabled), {
    const action = UTF8ToString(id);
    const root = document.getElementById('semantics');
    let button = Array.from(root.children).find(child => child.dataset.action === action);
    if (!button) {
        button = document.createElement('button'); button.dataset.action = action;
        button.addEventListener('click', () => Module.ccall('phoenix_semantic_action', null, ['string'], [action]));
        button.addEventListener('focus', () => Module.ccall('phoenix_semantic_focus', null, ['string'], [action]));
        root.appendChild(button);
    }
    button.textContent = UTF8ToString(label); delete button.dataset.stale;
    button.disabled = !enabled;
    const rect = Module.canvas.getBoundingClientRect(), sx=rect.width/Module.canvas.width, sy=rect.height/Module.canvas.height;
    if(width>0 && height>0) {
        button.className='placed';
        Object.assign(button.style,{left:`${rect.left+x*sx}px`,top:`${rect.top+y*sy}px`,width:`${width*sx}px`,height:`${height*sy}px`});
    } else { button.className='fallback'; button.removeAttribute('style'); }
    if(focused && root.contains(document.activeElement) && document.activeElement!==button) button.focus({preventScroll:true});
});
EM_JS(void, end_semantics, (), {
    const root = document.getElementById('semantics');
    for (const child of Array.from(root.children)) if (child.dataset.stale) child.remove();
});
EM_JS(void, update_semantic_status, (const char* text), {
    const status = document.getElementById('player-status');
    if (status && status.textContent !== UTF8ToString(text)) status.textContent = UTF8ToString(text);
});
// clang-format on
namespace phoenix
{
void publish_semantics(const Application &app)
{
	static auto next_status = std::chrono::steady_clock::time_point{};
	const auto now = std::chrono::steady_clock::now();
	if (now >= next_status)
	{
		update_semantic_status(app.accessible_status.c_str());
		next_status = now + std::chrono::seconds(1);
	}
	auto controls = navigation_controls(app);
	begin_semantics();
	for (const auto &control : controls)
		append_semantic_button(control.id.c_str(), control.label.c_str(), control.placement.x, control.placement.y,
		                       control.placement.width, control.placement.height, control.placement.focused,
		                       control.placement.enabled);
	end_semantics();
}
void drain_semantic_actions(Application &app)
{
	while (!focuses.empty())
	{
		if (app.focus_control)
			app.focus_control(focuses.front());
		focuses.pop_front();
	}
	while (!actions.empty())
	{
		invoke_navigation(app, actions.front());
		actions.pop_front();
	}
}
void install_native_accessibility(void *)
{
}
void remove_native_accessibility(void *)
{
}
} // namespace phoenix
