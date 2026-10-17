module;
#include <emscripten.h>
#include <deque>
#include <map>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

module phoenix.persistence;
#include "../replication/store_internal.hpp"

namespace
{
struct BrowserState
{
	std::deque<phoenix::PeerStoreResult> results;
	phoenix::WireJson recovered;
	bool ready = false, busy = true, failed = false;
};
std::map<unsigned, BrowserState *> stores;
unsigned next_store = 1;
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_store_result(unsigned id, double generation, int opened, int success,
                                                          const char *journal, const char *error)
{
	auto found = stores.find(id);
	if (found == stores.end())
		return;
	auto &state = *found->second;
	phoenix::PeerStoreResult result{opened != 0, static_cast<std::uint64_t>(generation), nullptr,
	                                error ? error : "Browser peer storage allocation failed"};
	try
	{
		if (!success)
			throw std::runtime_error(result.error.empty() ? "Browser peer storage failed" : result.error);
		if (opened)
		{
			result.restored = phoenix::storage::replay(journal, true).state;
			state.recovered = result.restored;
		}
	}
	catch (const std::exception &failure)
	{
		result.error = failure.what();
		state.failed = true;
	}
	state.results.push_back(std::move(result));
}
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, open_browser_store, (unsigned id, const char* location), {
    const namespace = UTF8ToString(location);
    const registry = Module.phoenixPeerStores || (Module.phoenixPeerStores = new Map());
    const state = {db:null,size:0,closed:false,pending:null}; registry.set(id,state);
    const reply = (generation,opened,success,journal,error) => {
        if (state.closed) return;
        let data = 0, problem = 0;
        try {
            const size = lengthBytesUTF8(journal)+1;
            if (size > 134217729) throw new Error('Peer journal exceeds 128 MiB');
            data = Module._malloc(size); problem = Module._malloc(lengthBytesUTF8(error)+1);
            if (!data || !problem) throw new Error('Browser peer storage allocation failed');
            stringToUTF8(journal,data,size); stringToUTF8(error,problem,lengthBytesUTF8(error)+1);
            Module.ccall('phoenix_store_result',null,['number','number','number','number','number','number'],[id,generation,opened?1:0,success?1:0,data,problem]);
        } catch (failure) {
            Module.ccall('phoenix_store_result',null,['number','number','number','number','number','number'],[id,generation,opened?1:0,0,0,0]);
        } finally { if (data) Module._free(data); if (problem) Module._free(problem); }
    };
    state.reply = reply;
    const begin = () => { try {
        const request = indexedDB.open('phoenix-v3-peer:'+namespace,1);
        request.onupgradeneeded = () => request.result.createObjectStore('journal',{autoIncrement:true});
        request.onerror = () => reply(0,true,false,"",String(request.error));
        request.onblocked = () => reply(0,true,false,"",'Peer storage is blocked by another tab');
        request.onsuccess = () => {
            if (state.closed) { request.result.close(); return; }
            state.db = request.result; state.db.onversionchange = () => state.db.close();
            const tx = state.db.transaction('journal','readonly'); state.pending = tx; const records = [];
            const cursor = tx.objectStore('journal').openCursor();
            cursor.onsuccess = () => {
                const entry = cursor.result; if (!entry) return;
                if (typeof entry.value !== 'string') { tx.abort(); return; }
                state.size += lengthBytesUTF8(entry.value);
                if (state.size > 134217728) { tx.abort(); return; }
                records.push(entry.value); entry.continue();
            };
            tx.oncomplete = () => { state.pending = null; reply(0,true,true,records.join(""),""); if (state.closed && state.release) state.release(); };
            tx.onabort = () => { state.pending = null; reply(0,true,false,"",'Cannot read complete bounded peer journal: '+String(tx.error)); if (state.closed && state.release) state.release(); };
        };
    } catch (error) { reply(0,true,false,"",String(error)); } };
    if (!navigator.locks) { reply(0,true,false,"",'Exclusive peer storage locks are unavailable'); return; }
    navigator.locks.request('phoenix-v3-peer:'+namespace,{ifAvailable:true}, lock => {
        if (!lock) { reply(0,true,false,"",'This peer identity is already open in another tab'); return; }
        return new Promise(resolve => { state.release = resolve; if (state.closed) resolve(); else begin(); });
    }).catch(error => reply(0,true,false,"",String(error)));
});
EM_JS(void, write_browser_store, (unsigned id, double generation, const char* bytes), {
    const state = Module.phoenixPeerStores.get(id); if (!state || state.closed) return;
    try {
        const record = UTF8ToString(bytes), size = lengthBytesUTF8(record);
        if (state.size+size > 134217728) throw new Error('Peer journal full; checkpoint required');
        const tx = state.db.transaction('journal','readwrite',{durability:'strict'});
        if (tx.durability !== 'strict') { tx.abort(); throw new Error('Strict durable transactions are unavailable'); }
        state.pending = tx;
        tx.oncomplete = () => { state.pending = null; state.size += size; state.reply(generation,false,true,"",""); if (state.closed && state.release) state.release(); };
        tx.onabort = () => { state.pending = null; state.reply(generation,false,false,"",String(tx.error || 'Peer transaction aborted')); if (state.closed && state.release) state.release(); };
        tx.objectStore('journal').add(record);
    } catch (error) { if (state.pending) state.pending.abort(); else state.reply(generation,false,false,"",String(error)); }
});
EM_JS(void, close_browser_store, (unsigned id), {
    const registry = Module.phoenixPeerStores; if (!registry) return;
    const state = registry.get(id); if (!state) return;
    state.closed = true; if (state.db) state.db.close(); if (!state.pending && state.release) state.release(); registry.delete(id);
});
// clang-format on
namespace phoenix
{
namespace
{
class BrowserStore final : public PeerStore
{
	unsigned id_;
	BrowserState state_;

public:
	explicit BrowserStore(const std::string &location) : id_(next_store++)
	{
		stores[id_] = &state_;
		open_browser_store(id_, location.c_str());
	}
	~BrowserStore() override
	{
		stores.erase(id_);
		close_browser_store(id_);
	}
	bool write(const DurableWrite &transaction) override
	{
		if (!state_.ready || state_.busy || state_.failed)
			return false;
		const auto record = storage::encode(transaction.transaction);
		apply_peer_write(state_.recovered, transaction.transaction);
		state_.busy = true;
		write_browser_store(id_, static_cast<double>(transaction.generation), record.c_str());
		return true;
	}
	std::optional<PeerStoreResult> poll() override
	{
		if (state_.results.empty())
			return {};
		auto result = std::move(state_.results.front());
		state_.results.pop_front();
		state_.busy = false;
		if (result.opened && result.error.empty())
			state_.ready = true;
		return result;
	}
};
} // namespace
std::unique_ptr<PeerStore> open_peer_store(const std::string &location)
{
	return std::make_unique<BrowserStore>(location);
}
} // namespace phoenix
