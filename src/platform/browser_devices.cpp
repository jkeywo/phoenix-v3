module;
#include <emscripten.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

module phoenix.devices;
namespace
{
std::vector<phoenix::DeviceEvent> pending;
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_device_event(int kind, double id, float x, float y, const char *device,
                                                          const char *control, float value)
{
	if (pending.size() < 4096)
		pending.push_back({static_cast<phoenix::DeviceEventKind>(kind), static_cast<std::uint64_t>(id), x, y,
		                   device ? device : "", control ? control : "", value});
}
// clang-format off
EM_JS(void, begin_devices, (), {
    const canvas = Module.canvas, abort = new AbortController(), contacts = new Map();
    Module.phoenixDevices = {abort,contacts,pads:new Map()}; canvas.style.touchAction = 'none';
    const send = (kind,id,x,y,device="",control="",value=0) => Module.ccall('phoenix_device_event',null,['number','number','number','number','string','string','number'],[kind,id,x,y,device,control,value]);
    Module.phoenixDevices.send = send;
    // GLFW's legacy touch listeners must not synthesize a second mouse action.
    for (const name of ['touchstart','touchmove','touchend','touchcancel']) canvas.addEventListener(name,event => {
        event.preventDefault(); event.stopImmediatePropagation();
    },{capture:true,passive:false,signal:abort.signal});
    for (const [name,kind] of [['pointerdown',0],['pointermove',1],['pointerup',2],['pointercancel',3],['lostpointercapture',3]]) canvas.addEventListener(name,event => {
        if (event.pointerType === 'mouse') return;
        const rect=canvas.getBoundingClientRect(), x=(event.clientX-rect.left)*canvas.width/rect.width, y=(event.clientY-rect.top)*canvas.height/rect.height;
        if (kind===0) { canvas.focus({preventScroll:true}); contacts.set(event.pointerId,[x,y]); canvas.setPointerCapture(event.pointerId); }
        else if (!contacts.has(event.pointerId)) return;
        send(kind,event.pointerId,x,y);
        if (kind>=2) contacts.delete(event.pointerId); else contacts.set(event.pointerId,[x,y]);
        event.preventDefault(); event.stopImmediatePropagation();
    },{capture:true,passive:false,signal:abort.signal});
    window.addEventListener('blur',()=>{ for(const [id,[x,y]] of contacts) send(3,id,x,y); contacts.clear(); },{signal:abort.signal});
});
EM_JS(void, read_gamepads, (), {
    const state=Module.phoenixDevices; if(!state) return;
    const seen=new Set(), controls=[[12,'game.faster'],[13,'game.slower'],[14,'game.left'],[15,'game.right'],[0,'game.contacts']];
    for(const pad of navigator.getGamepads?.() || []) if(pad?.connected) {
        const id=String(pad.index); seen.add(id);
        let held=state.pads.get(id); if(!held) { held=new Map(); state.pads.set(id,held); state.send(5,0,0,0,id); }
        for(const [index,control] of controls) { const value=document.hasFocus() && pad.buttons[index]?.pressed ? 1 : 0; if((held.get(control)||0)!==value) {state.send(4,0,0,0,id,control,value);held.set(control,value);} }
    }
    for(const [id] of state.pads) if(!seen.has(id)) {state.send(6,0,0,0,id);state.pads.delete(id);}
});
EM_JS(void, end_devices, (), { Module.phoenixDevices?.abort.abort(); delete Module.phoenixDevices; });
// clang-format on
namespace phoenix
{
void open_devices(void *)
{
	begin_devices();
}
void close_devices(void *)
{
	end_devices();
	pending.clear();
}
std::vector<DeviceEvent> poll_devices()
{
	read_gamepads();
	auto result = std::move(pending);
	pending.clear();
	return result;
}
} // namespace phoenix
