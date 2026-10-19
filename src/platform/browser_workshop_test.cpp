module;
#include <emscripten.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.tools;
import phoenix.content;
import phoenix.presentation;

namespace
{
std::optional<phoenix::Json> workshop_feedback;
std::string workshop_status;
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_workshop_worker_feedback(const char *text)
{
	try
	{
		if (text)
			workshop_feedback = phoenix::Json::parse(text);
	}
	catch (const std::exception &error)
	{
		workshop_feedback = phoenix::Json{{"running", false}, {"status", error.what()}};
	}
}
// clang-format off: embedded JavaScript must retain operators and optional chaining.
EM_JS(void, send_workshop_worker_request, (const char *header, const std::uint8_t *pack, unsigned pack_size, const std::uint8_t *dependency_bytes, unsigned dependency_size), {
    const reply = (feedback) => {
        const text = JSON.stringify(feedback);
        const size = lengthBytesUTF8(text) + 1;
        const pointer = Module._malloc(size);
        if (!pointer) return;
        try { stringToUTF8(text, pointer, size); Module._phoenix_workshop_worker_feedback(pointer); }
        finally { Module._free(pointer); }
    };
    try {
        const config = JSON.parse(UTF8ToString(header));
        const queued = Module.phoenixWorkshopTestPending || 0;
        const queuedBytes = Module.phoenixWorkshopTestPendingBytes || 0;
        const bytes = pack_size + dependency_size;
        if (queued >= 16 || queuedBytes + bytes > 134217728) throw new Error('Disposable test request queue is full. Retry when the current request finishes.');
        let worker = Module.phoenixWorkshopTestWorker;
        if (!worker) {
            worker = new Worker(new URL('workshop-test-worker.js', document.baseURI));
            Module.phoenixWorkshopTestWorker = worker;
            worker.onmessage = (event) => {
                if (Module.phoenixWorkshopTestWorker !== worker) return;
                if (event.data?.type === 'feedback') reply(event.data.feedback);
                if (event.data?.type === 'ack') {
                    Module.phoenixWorkshopTestPending = Math.max(0,(Module.phoenixWorkshopTestPending || 0)-1);
                    Module.phoenixWorkshopTestPendingBytes = Math.max(0,(Module.phoenixWorkshopTestPendingBytes || 0)-event.data.bytes);
                }
            };
            worker.onerror = (event) => {
                if (Module.phoenixWorkshopTestWorker !== worker) return;
                worker.terminate(); Module.phoenixWorkshopTestWorker = null;
                Module.phoenixWorkshopTestPending = 0; Module.phoenixWorkshopTestPendingBytes = 0;
                reply({running:false,status:event.message || 'Disposable test worker failed. Retry Start.'});
            };
        }
        const transfers = [];
        config.type = 'request';
        if (config.action === 'start') {
            config.pack = HEAPU8.slice(pack, pack + pack_size).buffer;
            transfers.push(config.pack);
            let offset = 0;
            config.dependencies = config.dependency_lengths.map((length) => {
                if (offset + length > dependency_size) throw new Error('Invalid dependency transfer boundary');
                const bytes = HEAPU8.slice(dependency_bytes + offset, dependency_bytes + offset + length).buffer;
                offset += length; transfers.push(bytes); return bytes;
            });
        }
        worker.postMessage(config, transfers);
        Module.phoenixWorkshopTestPending = queued + 1; Module.phoenixWorkshopTestPendingBytes = queuedBytes + bytes;
    } catch (error) { reply({status:String(error?.message || error),worker_error:true}); }

});
// clang-format on
namespace phoenix
{
void pump_browser_workshop_test(Application &app)
{
	if (auto pending = take_gameplay_test_request())
	{
		try
		{
			if (pending->pack.size() > 32 * 1024 * 1024)
				throw std::runtime_error("Disposable test archive exceeds 32 MiB.");
			Json metadata{{"mission", pending->mission},
			              {"slot", pending->slot},
			              {"ship", pending->ship},
			              {"dependency_lengths", Json::array()}};
			switch (pending->action)
			{
			case GameplayTestAction::Start:
				metadata["action"] = "start";
				break;
			case GameplayTestAction::Pause:
				metadata["action"] = "pause";
				break;
			case GameplayTestAction::Step:
				metadata["action"] = "step";
				break;
			case GameplayTestAction::Restart:
				metadata["action"] = "restart";
				break;
			case GameplayTestAction::Stop:
				metadata["action"] = "stop";
				break;
			}
			Bytes dependencies;
			if (pending->dependencies.size() > 64)
				throw std::runtime_error("Disposable test has more than 64 dependencies.");
			for (const auto &dependency : pending->dependencies)
			{
				if (dependency.size() > 32 * 1024 * 1024 ||
				    dependencies.size() + dependency.size() + pending->pack.size() > 128 * 1024 * 1024)
					throw std::runtime_error("Disposable test archive transfer exceeds its finite limit.");
				metadata["dependency_lengths"].push_back(dependency.size());
				dependencies.insert(dependencies.end(), dependency.begin(), dependency.end());
			}
			const auto text = metadata.dump();
			send_workshop_worker_request(text.c_str(), pending->pack.data(),
			                             static_cast<unsigned>(pending->pack.size()), dependencies.data(),
			                             static_cast<unsigned>(dependencies.size()));
		}
		catch (const std::exception &error)
		{
			app.status = error.what();
			set_gameplay_test_feedback({{"running", false}, {"status", error.what()}});
		}
	}
	if (workshop_feedback)
	{
		const auto status = workshop_feedback->value("status", std::string{});
		if (status != workshop_status)
		{
			app.status = status;
			workshop_status = status;
		}
		set_gameplay_test_feedback(*workshop_feedback);
		workshop_feedback.reset();
	}
}
} // namespace phoenix
