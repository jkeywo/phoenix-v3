#include <phoenix/transport.hpp>
#include <nlohmann/json.hpp>
#include <emscripten.h>
#include <deque>
#include <map>
namespace {
struct Queue { std::deque<phoenix::LinkNotice> notices; std::size_t bytes = 0; bool overflow = false; };
std::map<unsigned,Queue*> queues;
unsigned next_link = 1;
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_link_notice(unsigned id, int kind, const char* peer, const char* bytes, int direct) {
    auto found = queues.find(id); if (found == queues.end()) return;
    auto& queue = *found->second; const std::string data = bytes ? bytes : "";
    if (queue.notices.size() >= 1024 || queue.bytes+data.size() > 8*1024*1024) { queue.overflow = true; return; }
    queue.bytes += data.size(); queue.notices.push_back({static_cast<phoenix::LinkEvent>(kind),peer ? peer : "",data,direct != 0});
}
EM_JS(void, open_browser_transport, (unsigned id, const char* configuration), {
    const config = JSON.parse(UTF8ToString(configuration));
    const registry = Module.phoenixLinks || (Module.phoenixLinks = new Map());
    const state = {socket:null,peers:new Map(),present:new Set(),closed:false,retry:null}; registry.set(id,state);
    const notice = (kind,peer="",data="",direct=false) => {
        if (state.closed) return;
        const psize=lengthBytesUTF8(peer)+1, dsize=lengthBytesUTF8(data)+1;
        if (dsize>262145 || psize>129) return;
        const p=Module._malloc(psize), d=Module._malloc(dsize);
        if (!p || !d) { if (p) Module._free(p); if (d) Module._free(d); return; }
        try { stringToUTF8(peer,p,psize); stringToUTF8(data,d,dsize); Module.ccall('phoenix_link_notice',null,['number','number','number','number','number'],[id,config.events[kind],p,d,direct?1:0]); }
        finally { Module._free(p); Module._free(d); }
    };
    const envelope = (op,to,data) => {
        if (!state.socket || state.socket.readyState !== WebSocket.OPEN || state.socket.bufferedAmount>4194304) return false;
        state.socket.send(JSON.stringify({schema:1,op,to,data})); return true;
    };
    const attach = (peer,channel) => {
        if (channel.label !== 'phoenix') { channel.close(); return; }
        const link=state.peers.get(peer); if (!link) { channel.close(); return; }
        if (link.channel && link.channel !== channel) link.channel.close(); link.channel=channel;
        channel.onopen=()=>notice('direct_ready',peer,"",true);
        channel.onclose=()=>notice('direct_lost',peer);
        channel.onerror=()=>notice('error',peer,'Direct channel failed; relay remains available');
        channel.onmessage=event=> { if (typeof event.data === 'string' && lengthBytesUTF8(event.data)<=65536) notice('packet',peer,event.data,true); };
        if (channel.readyState === 'open') notice('direct_ready',peer,"",true);
    };
    const create = (peer,offer) => {
        if (state.peers.has(peer)) return state.peers.get(peer);
        if (state.peers.size>=32) throw new Error('Direct peer capacity reached; use available relay routes');
        const pc=new RTCPeerConnection({iceServers:config.ice_servers.map(url=>({urls:url}))});
        const link={pc,channel:null,remote:false,candidates:[]}; state.peers.set(peer,link);
        pc.onicecandidate=event=> { if (event.candidate) envelope('signal',peer,JSON.stringify({candidate:event.candidate.candidate,mid:event.candidate.sdpMid||'0'})); };
        pc.ondatachannel=event=>attach(peer,event.channel);
        pc.onconnectionstatechange=()=> { if (['failed','closed'].includes(pc.connectionState)) notice('direct_lost',peer); };
        if (offer) {
            attach(peer,pc.createDataChannel('phoenix',{ordered:true}));
            pc.createOffer().then(description=>pc.setLocalDescription(description)).then(()=>envelope('signal',peer,JSON.stringify({type:pc.localDescription.type,sdp:pc.localDescription.sdp}))).catch(error=>notice('error',peer,String(error)));
        }
        return link;
    };
    const discover = peer => {
        if (typeof peer !== 'string' || !/^[A-Za-z0-9._-]{1,128}$/.test(peer) || peer === config.self || state.present.size>=32) return;
        if (!state.present.has(peer)) { state.present.add(peer); notice('discovered',peer); }
        if (!config.force_relay && config.self<peer) create(peer,true);
    };
    const signal = async (peer,data) => {
        if (config.force_relay || !state.present.has(peer)) return;
        try {
            const value=JSON.parse(data), link=create(peer,false);
            if (typeof value.sdp === 'string' && ['offer','answer'].includes(value.type)) {
                await link.pc.setRemoteDescription({type:value.type,sdp:value.sdp}); link.remote=true;
                for (const candidate of link.candidates) await link.pc.addIceCandidate(candidate); link.candidates=[];
                if (value.type === 'offer') { await link.pc.setLocalDescription(await link.pc.createAnswer()); envelope('signal',peer,JSON.stringify({type:link.pc.localDescription.type,sdp:link.pc.localDescription.sdp})); }
            } else if (typeof value.candidate === 'string') {
                const candidate={candidate:value.candidate,sdpMid:value.mid||'0'};
                if (link.remote) await link.pc.addIceCandidate(candidate); else if (link.candidates.length<64) link.candidates.push(candidate);
            }
        } catch (error) { notice('error',peer,String(error)); }
    };
    const connect = () => {
        if (state.closed) return;
        try {
            const socket=new WebSocket(config.url); state.socket=socket;
            socket.onopen=()=>socket.send(JSON.stringify({schema:1,op:'hello',room:config.room,peer:config.self}));
            socket.onclose=()=> { if (state.closed) return; notice('signalling_lost'); state.retry=setTimeout(connect,2000); };
            socket.onerror=()=>notice('error',"",'Signalling connection failed');
            socket.onmessage=event=> {
                try {
                    if (typeof event.data !== 'string' || lengthBytesUTF8(event.data)>262144) return;
                    const value=JSON.parse(event.data); if (value.schema !== 1) return;
                    if (value.op === 'ready') { notice('signalling_ready'); for (const peer of value.peers) discover(peer); }
                    else if (value.op === 'arrived') discover(value.peer);
                    else if (value.op === 'left') { state.present.delete(value.peer); notice('discovery_lost',value.peer); }
                    else if (value.op === 'signal') signal(value.from,value.data);
                    else if (value.op === 'relay' && state.present.has(value.from)) notice('packet',value.from,value.data,false);
                    else if (value.op === 'error') notice('error',"",value.message);
                } catch (error) { notice('error',"",String(error)); }
            };
        } catch (error) { notice('error',"",String(error)); state.retry=setTimeout(connect,2000); }
    };
    state.send=(peer,data)=> {
        if (lengthBytesUTF8(data)>65536) return false;
        const channel=state.peers.get(peer)?.channel;
        if (!config.force_relay && channel?.readyState === 'open') {
            if (channel.bufferedAmount>4194304) return false;
            try { channel.send(data); return true; } catch {}
        }
        return state.present.has(peer) && envelope('relay',peer,data);
    };
    connect();
});
EM_JS(bool, send_browser_packet, (unsigned id, const char* peer, const char* bytes), {
    return Module.phoenixLinks.get(id)?.send(UTF8ToString(peer),UTF8ToString(bytes)) || false;
});
EM_JS(void, close_browser_transport, (unsigned id), {
    const registry=Module.phoenixLinks; if (!registry) return;
    const state=registry.get(id); if (!state) return;
    state.closed=true; if (state.retry) clearTimeout(state.retry); if (state.socket) state.socket.close();
    for (const link of state.peers.values()) { if (link.channel) link.channel.close(); link.pc.close(); } registry.delete(id);
});
namespace phoenix {
namespace {
class BrowserTransport final : public PeerTransport {
    unsigned id_;
    Queue queue_;
public:
    explicit BrowserTransport(const LinkOptions& options) : id_(next_link++) {
        verify_link_options(options);
        queues[id_] = &queue_;
        const nlohmann::json config{{"url",options.url},{"room",options.room},{"self",options.self},{"force_relay",options.force_relay},{"ice_servers",options.ice_servers},{"events",{
            {"signalling_ready",static_cast<int>(LinkEvent::signalling_ready)},{"signalling_lost",static_cast<int>(LinkEvent::signalling_lost)},{"discovered",static_cast<int>(LinkEvent::discovered)},{"discovery_lost",static_cast<int>(LinkEvent::discovery_lost)},
            {"direct_ready",static_cast<int>(LinkEvent::direct_ready)},{"direct_lost",static_cast<int>(LinkEvent::direct_lost)},{"packet",static_cast<int>(LinkEvent::packet)},{"error",static_cast<int>(LinkEvent::error)}}}};
        open_browser_transport(id_,config.dump().c_str());
    }
    ~BrowserTransport() override { queues.erase(id_); close_browser_transport(id_); }
    std::vector<LinkNotice> poll() override {
        std::vector<LinkNotice> result; if (queue_.overflow) result.push_back({LinkEvent::error,{},"Transport receive backpressure; packets dropped"});
        while (!queue_.notices.empty()) { result.push_back(std::move(queue_.notices.front())); queue_.notices.pop_front(); }
        queue_.bytes = 0; queue_.overflow = false; return result;
    }
    bool send(const std::string& peer, const std::string& bytes) override { return bytes.size() <= 65536 && peer.find('\0') == std::string::npos && bytes.find('\0') == std::string::npos && send_browser_packet(id_,peer.c_str(),bytes.c_str()); }
};
}
std::unique_ptr<PeerTransport> open_peer_transport(const LinkOptions& options) { return std::make_unique<BrowserTransport>(options); }
}
