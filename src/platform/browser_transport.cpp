module;
#include "browser_main.hpp"
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>
#include <emscripten.h>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

module phoenix.transport;

namespace
{
enum class BrowserEvent
{
	ws_open,
	ws_closed,
	ws_packet,
	signal,
	packet,
	opened,
	lost,
	error
};
struct Incoming
{
	BrowserEvent kind;
	std::string peer, data;
};
struct Queue
{
	std::deque<Incoming> events;
	std::size_t bytes = 0;
	bool overflow = false;
};
std::map<unsigned, Queue *> queues;
std::mutex registry_mutex;
std::atomic<unsigned> next_link{1};
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_link_notice(unsigned id, int kind, const char *peer, const char *bytes,
                                                         int)
{
	std::lock_guard lock(registry_mutex);
	auto found = queues.find(id);
	if (found == queues.end())
		return;
	auto &queue = *found->second;
	const std::string data = bytes ? bytes : "";
	if (queue.events.size() >= phoenix::link_queue_limit || queue.bytes + data.size() > phoenix::link_queue_bytes)
	{
		queue.overflow = true;
		return;
	}
	queue.bytes += data.size();
	queue.events.push_back({static_cast<BrowserEvent>(kind), peer ? peer : "", data});
}
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, open_browser_transport_js, (unsigned id, const char* configuration), {
    const config = JSON.parse(UTF8ToString(configuration));
    const registry = Module.phoenixLinks || (Module.phoenixLinks = new Map());
    const state = {socket:null,peers:new Map(),closed:false,retry:null}; registry.set(id,state);
    const notice = (kind,peer="",data="") => {
        if (state.closed) return;
        const psize=lengthBytesUTF8(peer)+1, dsize=lengthBytesUTF8(data)+1;
        if (dsize>config.envelope_limit+1 || psize>129) return;
        const p=Module._malloc(psize), d=Module._malloc(dsize);
        if (!p || !d) { if (p) Module._free(p); if (d) Module._free(d); return; }
        try { stringToUTF8(peer,p,psize); stringToUTF8(data,d,dsize); Module.ccall('phoenix_link_notice',null,['number','number','number','number','number'],[id,config.events[kind],p,d,0]); }
        finally { Module._free(p); Module._free(d); }
    };
    const attach = (peer,channel) => {
        if (channel.label !== 'phoenix') { channel.close(); return; }
        const link=state.peers.get(peer); if (!link) { channel.close(); return; }
        if (link.channel && link.channel !== channel) link.channel.close(); link.channel=channel;
        channel.onopen=()=>notice('opened',peer);
        channel.onclose=()=>notice('lost',peer);
        channel.onerror=()=>notice('error',peer,'Direct channel failed; relay remains available');
        channel.onmessage=event=> { if (typeof event.data === 'string' && lengthBytesUTF8(event.data)<=config.packet_limit) notice('packet',peer,event.data); };
        if (channel.readyState === 'open') notice('opened',peer);
    };
    const create = (peer,offer) => {
        if (state.peers.has(peer)) return state.peers.get(peer);
        if (state.peers.size>=config.peer_limit) throw new Error('Direct peer capacity reached; use available relay routes');
        const pc=new RTCPeerConnection({iceServers:config.ice_servers.map(url=>({urls:url}))});
        const link={pc,channel:null,remote:false,candidates:[]}; state.peers.set(peer,link);
        pc.onicecandidate=event=> { if (event.candidate) notice('signal',peer,JSON.stringify({candidate:event.candidate.candidate,mid:event.candidate.sdpMid||'0'})); };
        pc.ondatachannel=event=>attach(peer,event.channel);
        pc.onconnectionstatechange=()=> { if (['failed','closed'].includes(pc.connectionState)) notice('lost',peer); };
        if (offer) {
            attach(peer,pc.createDataChannel('phoenix',{ordered:true}));
            pc.createOffer().then(description=>pc.setLocalDescription(description)).then(()=>notice('signal',peer,JSON.stringify({type:pc.localDescription.type,sdp:pc.localDescription.sdp}))).catch(error=>notice('error',peer,String(error)));
        }
        return link;
    };
    state.reset = peer => {
        const link=state.peers.get(peer); if (!link) return;
        state.peers.delete(peer);
        if (link.channel) {
            link.channel.onopen=link.channel.onclose=link.channel.onerror=link.channel.onmessage=null;
            link.channel.close();
        }
        link.pc.onicecandidate=link.pc.ondatachannel=link.pc.onconnectionstatechange=null;
        link.pc.close();
    };
    state.initiate = peer => {
        try { create(peer,true); } catch (error) { notice('error',peer,String(error)); }
    };
    state.signal = async (peer,data) => {
        try {
            const value=JSON.parse(data), link=create(peer,false);
            if (typeof value.sdp === 'string' && ['offer','answer'].includes(value.type)) {
                await link.pc.setRemoteDescription({type:value.type,sdp:value.sdp}); link.remote=true;
                for (const candidate of link.candidates) await link.pc.addIceCandidate(candidate); link.candidates=[];
                if (value.type === 'offer') { await link.pc.setLocalDescription(await link.pc.createAnswer()); notice('signal',peer,JSON.stringify({type:link.pc.localDescription.type,sdp:link.pc.localDescription.sdp})); }
            } else if (typeof value.candidate === 'string') {
                const candidate={candidate:value.candidate,sdpMid:value.mid||'0'};
                if (link.remote) await link.pc.addIceCandidate(candidate); else if (link.candidates.length<config.candidate_limit) link.candidates.push(candidate);
            }
        } catch (error) { notice('error',peer,String(error)); }
    };
    const connect = () => {
        if (state.closed) return;
        try {
            const socket=new WebSocket(config.url); state.socket=socket;
            socket.onopen=()=>notice('ws_open');
            socket.onclose=()=> { if (state.closed) return; notice('ws_closed'); state.retry=setTimeout(connect,2000); };
            socket.onerror=()=>notice('error',"",'Signalling connection failed');
            socket.onmessage=event=> {
                if (typeof event.data === 'string' && lengthBytesUTF8(event.data)<=config.envelope_limit)
                    notice('ws_packet',"",event.data);
            };
        } catch (error) { notice('error',"",String(error)); state.retry=setTimeout(connect,2000); }
    };
    state.envelope = data => {
        if (!state.socket || state.socket.readyState !== WebSocket.OPEN || state.socket.bufferedAmount>config.buffer_limit) return false;
        try { state.socket.send(data); return true; } catch { return false; }
    };
    connect();
});
EM_JS(bool, send_browser_envelope_js, (unsigned id, const char* bytes), {
    return Module.phoenixLinks.get(id)?.envelope(UTF8ToString(bytes)) || false;
});
EM_JS(double, browser_direct_buffered_js, (unsigned id, const char* peer), {
    const channel=Module.phoenixLinks.get(id)?.peers.get(UTF8ToString(peer))?.channel;
    return channel?.readyState === 'open' ? channel.bufferedAmount : -1;
});
EM_JS(bool, send_browser_direct_js, (unsigned id, const char* peer, const char* bytes), {
    const channel=Module.phoenixLinks.get(id)?.peers.get(UTF8ToString(peer))?.channel;
    if (channel?.readyState !== 'open') return false;
    try { channel.send(UTF8ToString(bytes)); return true; } catch { return false; }
});
EM_JS(void, initiate_browser_peer_js, (unsigned id, const char* peer), {
    Module.phoenixLinks.get(id)?.initiate(UTF8ToString(peer));
});
EM_JS(void, reset_browser_peer_js, (unsigned id, const char* peer), {
    Module.phoenixLinks.get(id)?.reset(UTF8ToString(peer));
});
EM_JS(void, signal_browser_peer_js, (unsigned id, const char* peer, const char* bytes), {
    Module.phoenixLinks.get(id)?.signal(UTF8ToString(peer),UTF8ToString(bytes));
});
EM_JS(void, close_browser_transport_js, (unsigned id), {
    const registry=Module.phoenixLinks; if (!registry) return;
    const state=registry.get(id); if (!state) return;
    state.closed=true; if (state.retry) clearTimeout(state.retry); if (state.socket) state.socket.close();
    for (const link of state.peers.values()) { if (link.channel) link.channel.close(); link.pc.close(); } registry.delete(id);
});
// clang-format on

namespace
{
static void open_browser_transport(unsigned id, const char *configuration)
{
	return phoenix::browser::on_main(
	    [&]
	    {
		    return open_browser_transport_js(id, configuration);
	    });
}
static bool send_browser_envelope(unsigned id, const char *bytes)
{
	return phoenix::browser::on_main(
	    [&]
	    {
		    return send_browser_envelope_js(id, bytes);
	    });
}
static double browser_direct_buffered(unsigned id, const char *peer)
{
	return phoenix::browser::on_main(
	    [&]
	    {
		    return browser_direct_buffered_js(id, peer);
	    });
}
static bool send_browser_direct(unsigned id, const char *peer, const char *bytes)
{
	return phoenix::browser::on_main(
	    [&]
	    {
		    return send_browser_direct_js(id, peer, bytes);
	    });
}
static void initiate_browser_peer(unsigned id, const char *peer)
{
	return phoenix::browser::on_main(
	    [&]
	    {
		    return initiate_browser_peer_js(id, peer);
	    });
}
static void reset_browser_peer(unsigned id, const char *peer)
{
	return phoenix::browser::on_main(
	    [&]
	    {
		    return reset_browser_peer_js(id, peer);
	    });
}
static void signal_browser_peer(unsigned id, const char *peer, const char *bytes)
{
	return phoenix::browser::on_main(
	    [&]
	    {
		    return signal_browser_peer_js(id, peer, bytes);
	    });
}
static void close_browser_transport(unsigned id)
{
	return phoenix::browser::on_main(
	    [&]
	    {
		    return close_browser_transport_js(id);
	    });
}
} // namespace
namespace phoenix
{
namespace
{
class BrowserTransport final : public PeerTransport
{
	unsigned id_;
	LinkOptions options_;
	LinkPolicy policy_;
	Queue queue_;
	bool envelope(const std::string &op, const std::string &peer, const std::string &data)
	{
		const auto bytes = link_envelope(op, peer, data);
		return !bytes.empty() && send_browser_envelope(id_, bytes.c_str());
	}

public:
	explicit BrowserTransport(const LinkOptions &options) : id_(next_link++), options_(options), policy_(options)
	{
		verify_link_options(options);
		{
			std::lock_guard lock(registry_mutex);
			queues[id_] = &queue_;
		}
		const nlohmann::json config{{"url", options.url},
		                            {"ice_servers", options.ice_servers},
		                            {"packet_limit", link_packet_limit},
		                            {"envelope_limit", link_envelope_limit},
		                            {"buffer_limit", link_buffer_limit},
		                            {"peer_limit", link_peer_limit},
		                            {"candidate_limit", link_candidate_limit},
		                            {"events",
		                             {{"ws_open", static_cast<int>(BrowserEvent::ws_open)},
		                              {"ws_closed", static_cast<int>(BrowserEvent::ws_closed)},
		                              {"ws_packet", static_cast<int>(BrowserEvent::ws_packet)},
		                              {"signal", static_cast<int>(BrowserEvent::signal)},
		                              {"packet", static_cast<int>(BrowserEvent::packet)},
		                              {"opened", static_cast<int>(BrowserEvent::opened)},
		                              {"lost", static_cast<int>(BrowserEvent::lost)},
		                              {"error", static_cast<int>(BrowserEvent::error)}}}};
		open_browser_transport(id_, config.dump().c_str());
	}
	~BrowserTransport() override
	{
		{
			std::lock_guard lock(registry_mutex);
			queues.erase(id_);
		}
		close_browser_transport(id_);
	}
	std::vector<LinkNotice> poll() override
	{
		std::vector<LinkNotice> result;
		std::deque<Incoming> events;
		{
			std::lock_guard lock(registry_mutex);
			if (queue_.overflow)
				result.push_back({LinkEvent::error, {}, "Transport receive backpressure; packets dropped"});
			events.swap(queue_.events);
			queue_.bytes = 0;
			queue_.overflow = false;
		}
		for (const auto &event : events)
			try
			{
				switch (event.kind)
				{
				case BrowserEvent::ws_open:
					send_browser_envelope(id_, link_hello(options_).c_str());
					break;
				case BrowserEvent::ws_closed:
					result.push_back({LinkEvent::signalling_lost});
					break;
				case BrowserEvent::ws_packet:
					for (const auto &action : policy_.receive(event.data, result))
						if (action.kind == LinkAction::Kind::reset)
							reset_browser_peer(id_, action.peer.c_str());
						else if (action.kind == LinkAction::Kind::initiate)
							initiate_browser_peer(id_, action.peer.c_str());
						else
							signal_browser_peer(id_, action.peer.c_str(), action.data.c_str());
					break;
				case BrowserEvent::signal:
					envelope("signal", event.peer, event.data);
					break;
				case BrowserEvent::packet:
					result.push_back({LinkEvent::packet, event.peer, event.data, true});
					break;
				case BrowserEvent::opened:
					result.push_back({LinkEvent::direct_ready, event.peer, {}, true});
					break;
				case BrowserEvent::lost:
					result.push_back({LinkEvent::direct_lost, event.peer});
					break;
				case BrowserEvent::error:
					result.push_back({LinkEvent::error, event.peer, event.data});
					break;
				}
			}
			catch (const std::exception &error)
			{
				result.push_back({LinkEvent::error, event.peer, error.what()});
			}
		return result;
	}
	bool send(const std::string &peer, const std::string &bytes) override
	{
		const auto buffered = browser_direct_buffered(id_, peer.c_str());
		std::size_t buffered_bytes = 0;
		if (buffered > link_buffer_limit)
			buffered_bytes = link_buffer_limit + 1;
		else if (buffered >= 0)
			buffered_bytes = static_cast<std::size_t>(buffered);
		const auto route = policy_.route(peer, bytes, buffered >= 0, buffered_bytes);
		if (route == LinkRoute::unavailable)
			return false;
		if (route == LinkRoute::direct && send_browser_direct(id_, peer.c_str(), bytes.c_str()))
			return true;
		return policy_.route(peer, bytes, false, 0) == LinkRoute::relay && envelope("relay", peer, bytes);
	}
};
} // namespace
std::unique_ptr<PeerTransport> open_peer_transport(const LinkOptions &options)
{
	return std::make_unique<BrowserTransport>(options);
}
} // namespace phoenix
