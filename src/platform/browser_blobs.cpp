#include "../network/blob_internal.hpp"
#include <emscripten.h>
#include <deque>
#include <map>
namespace
{
struct State
{
	std::deque<phoenix::BlobResult> results;
	bool ready = false, busy = true;
};
std::map<unsigned, State *> states;
unsigned next = 1;
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_blob_result(unsigned id, double request, int opened, const char *key,
                                                         const unsigned char *bytes, unsigned size, int exists,
                                                         const char *error)
{
	auto found = states.find(id);
	if (found == states.end())
		return;
	phoenix::BlobResult result;
	result.opened = opened;
	result.request = static_cast<std::uint64_t>(request);
	result.key = key ? key : "";
	result.error = error ? error : "Browser blob allocation failed";
	try
	{
		if (result.error.empty() && exists)
		{
			if (size > phoenix::blobs::max_bytes + 1024)
				throw std::runtime_error("Persistent blob exceeds bounds");
			result.bytes = phoenix::blobs::decode(result.key, phoenix::Bytes(bytes, bytes + size));
		}
	}
	catch (const std::exception &failure)
	{
		result.error = failure.what();
	}
	found->second->results.push_back(std::move(result));
}
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, open_browser_blobs, (unsigned id,const char* location), {
    const namespace=UTF8ToString(location), registry=Module.phoenixBlobs||(Module.phoenixBlobs=new Map());
    const state={db:null,closed:false}; registry.set(id,state);
    state.reply=(request,opened,key,bytes,error) => {
        if (state.closed) return; let k=0,b=0,e=0;
        try {
            k=Module._malloc(lengthBytesUTF8(key)+1); e=Module._malloc(lengthBytesUTF8(error)+1);
            if (bytes) b=Module._malloc(bytes.length||1);
            if (!k || !e || (bytes && !b)) throw new Error('Blob allocation failed');
            stringToUTF8(key,k,lengthBytesUTF8(key)+1); stringToUTF8(error,e,lengthBytesUTF8(error)+1); if (bytes) HEAPU8.set(bytes,b);
            Module.ccall('phoenix_blob_result',null,['number','number','number','number','number','number','number','number'],[id,request,opened?1:0,k,b,bytes?bytes.length:0,bytes?1:0,e]);
        } catch(failure) { Module.ccall('phoenix_blob_result',null,['number','number','number','number','number','number','number','number'],[id,request,opened?1:0,0,0,0,0,0]); }
        finally { if(k)Module._free(k);if(b)Module._free(b);if(e)Module._free(e); }
    };
    try {
        const request=indexedDB.open('phoenix-v3-blobs:'+namespace,1);
        request.onupgradeneeded=()=>request.result.createObjectStore('blobs');
        request.onerror=()=>state.reply(0,true,"",null,String(request.error));
        request.onblocked=()=>state.reply(0,true,"",null,'Persistent blobs blocked by another tab');
        request.onsuccess=()=> { state.db=request.result; if (state.closed) {state.db.close();return;} state.db.onversionchange=()=>state.db.close(); state.reply(0,true,"",null,""); };
    } catch(error){state.reply(0,true,"",null,String(error));}
});
EM_JS(void, browser_blob_operation, (unsigned id,double request,const char* key,const unsigned char* bytes,unsigned size,int writing), {
    const state=Module.phoenixBlobs.get(id); if(!state||state.closed)return; const name=UTF8ToString(key);
    try {
        const tx=state.db.transaction('blobs',writing?'readwrite':'readonly',writing?{durability:'strict'}:undefined);
        if(writing&&tx.durability!=='strict'){tx.abort();throw new Error('Strict blob durability unavailable');}
        const operation=writing?tx.objectStore('blobs').put(HEAPU8.slice(bytes,bytes+size),name):tx.objectStore('blobs').get(name);
        let value=null;
        operation.onsuccess=()=> { if(!writing && operation.result!==undefined){const result=operation.result;if(!(result instanceof Uint8Array)||result.length>33555456){tx.abort();return;}value=result;} };
        tx.oncomplete=()=>state.reply(request,false,name,value,"");
        tx.onabort=()=>state.reply(request,false,name,null,String(tx.error||'Persistent blob operation aborted'));
    }catch(error){state.reply(request,false,name,null,String(error));}
});
EM_JS(void, close_browser_blobs,(unsigned id),{const registry=Module.phoenixBlobs;if(!registry)return;const state=registry.get(id);if(!state)return;state.closed=true;if(state.db)state.db.close();registry.delete(id);});
// clang-format on
namespace phoenix
{
namespace
{
class BrowserBlobs final : public BlobStore
{
	unsigned id_;
	State state_;

public:
	explicit BrowserBlobs(const std::string &location) : id_(next++)
	{
		states[id_] = &state_;
		open_browser_blobs(id_, location.c_str());
	}
	~BrowserBlobs() override
	{
		states.erase(id_);
		close_browser_blobs(id_);
	}
	bool read(std::uint64_t request, const std::string &key) override
	{
		if (!state_.ready || state_.busy)
			return false;
		blobs::verify_key(key);
		state_.busy = true;
		browser_blob_operation(id_, static_cast<double>(request), key.c_str(), nullptr, 0, false);
		return true;
	}
	bool write(std::uint64_t request, const std::string &key, const Bytes &bytes) override
	{
		if (!state_.ready || state_.busy)
			return false;
		auto encoded = blobs::encode(key, bytes);
		state_.busy = true;
		browser_blob_operation(id_, static_cast<double>(request), key.c_str(), encoded.data(),
		                       static_cast<unsigned>(encoded.size()), true);
		return true;
	}
	std::optional<BlobResult> poll() override
	{
		if (state_.results.empty())
			return {};
		auto result = std::move(state_.results.front());
		state_.results.pop_front();
		state_.busy = false;
		if (result.opened)
			state_.ready = result.error.empty();
		return result;
	}
};
} // namespace
std::unique_ptr<BlobStore> open_blob_store(const std::string &location)
{
	return std::make_unique<BrowserBlobs>(location);
}
} // namespace phoenix
