module;
#include <cstdint>
#include <algorithm>
#include <tuple>
#include <emscripten.h>
#include <deque>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module phoenix.platform.accessibility;

namespace
{
std::deque<std::string> actions;
std::deque<std::string> focuses;
std::deque<std::pair<std::string, std::string>> values;
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
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_semantic_value(const char *id, const char *value)
{
	if (values.size() < 64 && std::string(value).size() <= 4096)
		values.emplace_back(id, value);
}
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(bool, semantic_canvas_changed, (), {
    const c = Module.canvas;
    const geometry=[c.offsetLeft,c.offsetTop,c.clientWidth,c.clientHeight,c.width,c.height].join('/');
    const root=document.getElementById('semantics');
    const changed=root.phoenixGeometry!==geometry;root.phoenixGeometry=geometry;return changed;
});
EM_JS(void, begin_semantics, (), {
    const root = document.getElementById('semantics');
    root.phoenixControls = new Map();
    for (const child of root.querySelectorAll('[data-action]')) {
        root.phoenixControls.set(child.dataset.action, child);
        child.dataset.stale = 'true';
    }
    const canvas = Module.canvas;
    root.phoenixPlacement = {x:canvas.offsetLeft,y:canvas.offsetTop,sx:canvas.clientWidth/canvas.width,sy:canvas.clientHeight/canvas.height};
});
EM_JS(void, append_semantic_button, (const char* id, const char* label, float x, float y, float width, float height, bool focused, bool enabled, bool editable, const char* value, bool percentage, bool read_only, bool tab, bool selected, bool toggle, bool pressed), {
    const action = UTF8ToString(id);
    const root = document.getElementById('semantics');
    let button = root.phoenixControls.get(action);
    if (!button) {
        button = document.createElement(editable ? 'input' : read_only ? 'div' : 'button'); button.dataset.action = action; root.phoenixControls.set(action, button);
        if(editable) {
            button.type=percentage ? 'range' : 'text'; button.maxLength=4096;
            if(percentage) { button.min='0'; button.max='100'; button.step='1'; }
            button.addEventListener('input', () => Module.ccall('phoenix_semantic_value',null,['string','string'],[action,button.value]));
        } else if(!read_only) button.addEventListener('click', () => Module.ccall('phoenix_semantic_action', null, ['string'], [action]));
        if (tab) button.onkeydown = event => {
            const tabs = Array.from(root.querySelectorAll('[role=tab]'));
            const at = tabs.indexOf(button), n = tabs.length;
            const next = event.key==='ArrowLeft' ? (at+n-1)%n : event.key==='ArrowRight' ? (at+1)%n : event.key==='Home' ? 0 : event.key==='End' ? n-1 : -1;
            if(next>=0) { event.preventDefault(); event.stopPropagation(); tabs[next].focus(); tabs[next].click(); }
        };
        button.addEventListener('focus', () => Module.ccall('phoenix_semantic_focus', null, ['string'], [action]));
        if(tab) {
            let tabs=root.querySelector('#semantic-settings-tabs');
            if(!tabs) { tabs=document.createElement('div'); tabs.id='semantic-settings-tabs'; tabs.setAttribute('role','tablist'); tabs.style.display='contents'; root.append(tabs); }
            tabs.append(button);
        } else root.appendChild(button);
    }
    const text=UTF8ToString(label);
    if(editable) { button.setAttribute('aria-label',text); if(document.activeElement!==button)button.value=UTF8ToString(value); }
    else if(button.textContent!==text)button.textContent=text;
    if(tab) { button.setAttribute('role','tab'); button.setAttribute('aria-selected',String(!!selected)); button.tabIndex=selected?0:-1; }
    if(toggle) button.setAttribute('aria-pressed',String(!!pressed)); else button.removeAttribute('aria-pressed');
    delete button.dataset.stale;
    button.disabled = !enabled;
    const {x:cx,y:cy,sx,sy} = root.phoenixPlacement;
    if(width>0 && height>0) {
        button.className='placed';
        Object.assign(button.style,{left:`${cx+x*sx}px`,top:`${cy+y*sy}px`,width:`${width*sx}px`,height:`${height*sy}px`});
    } else { button.className='fallback'; button.removeAttribute('style'); }
    // DOM focus drives RmlUi through the queue. Re-focusing from the preceding
    // frame here would undo the user's Tab key before that queue is drained.
});
EM_JS(void, end_semantics, (const char* settings_label), {
    const root = document.getElementById('semantics');
    for (const child of root.querySelectorAll('[data-stale]')) child.remove();
    const tabs=root.querySelector('#semantic-settings-tabs');
    if(tabs) { if(!tabs.children.length) tabs.remove(); else tabs.setAttribute('aria-label',UTF8ToString(settings_label)); }
});
EM_JS(void, update_semantic_status, (const char* text), {
    const status = document.getElementById('player-status');
    if (status && status.textContent !== UTF8ToString(text)) status.textContent = UTF8ToString(text);
});
// clang-format on
namespace phoenix
{
void focus_semantic_text(const std::string &id)
{
	EM_ASM({
		const id = UTF8ToString($0);
		const input = Array.from(document.querySelectorAll('#semantics input')).find(item => item.dataset.action === id);
		if (input) input.focus();
	}, id.c_str());
}

void publish_accessibility(const SemanticSnapshot &frame)
{
	static auto next_status = std::chrono::steady_clock::time_point{};
	const auto now = std::chrono::steady_clock::now();
	if (now >= next_status)
	{
		update_semantic_status(frame.status.c_str());
		next_status = now + std::chrono::seconds(1);
	}
	static std::vector<SemanticControl> previous;
	static std::weak_ptr<void> owner;
	static std::uint64_t generation = 0;
	auto controls = frame.controls;
	const auto current_generation = frame.generation;
	const bool canvas_changed = semantic_canvas_changed();
	if (!canvas_changed && owner.lock() == frame.owner && generation == current_generation &&
	    same_semantic_controls(controls, previous))
		return;
	owner = frame.owner;
	generation = current_generation;
	begin_semantics();
	for (const auto &control : controls)
		append_semantic_button(control.id.c_str(), control.label.c_str(), control.placement.x, control.placement.y,
		                       control.placement.width, control.placement.height, control.placement.focused,
		                       control.placement.enabled, control.editable, control.value.c_str(), control.percentage,
		                       control.read_only, control.tab, control.selected, control.toggle, control.pressed);
	end_semantics(frame.settings_label.c_str());
	previous = std::move(controls);
}
bool semantic_actions_pending()
{
	return !actions.empty() || !focuses.empty() || !values.empty();
}
AccessibilityActions take_accessibility_actions(const SemanticSnapshot &)
{
	AccessibilityActions result;
	result.values_first = true;
	result.values.assign(values.begin(), values.end());
	values.clear();
	result.focuses.assign(focuses.begin(), focuses.end());
	focuses.clear();
	result.actions.assign(actions.begin(), actions.end());
	actions.clear();
	return result;
}
bool probe_accessibility(const AccessibilityProbe &)
{
	return false;
}
void install_native_accessibility(void *)
{
}
void remove_native_accessibility(void *)
{
}
} // namespace phoenix
