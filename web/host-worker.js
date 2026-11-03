/* Ordinary browser host. Only this worker owns the C++ lobby and World. */
let modulePromise;
let timer;

function call(module, operation, types = [], values = []) {
	const result = JSON.parse(module.ccall("phoenix_worker_host_" + operation, "string", types, values));
	if (!result.ok) throw Error(result.error);
	return result.value;
}

async function open(config) {
	if (modulePromise) throw Error("This worker already owns a host.");
	importScripts(new URL("phoenix_browser_host.js", self.location.href).href);
	modulePromise = self.createPhoenixBrowserHost({
		locateFile: (file) => new URL(file, self.location.href).href,
	});
	const module = await modulePromise;
	call(module, "open", ["string"], [JSON.stringify(config)]);
	let previous = performance.now();
	timer = setInterval(() => {
		try {
			const now = performance.now();
			self.postMessage({ type: "frame", frame: call(module, "frame", ["number"], [(now - previous) / 1000]) });
			previous = now;
		} catch (error) {
			clearInterval(timer);
			self.postMessage({ type: "error", error: String(error.message || error) });
		}
	}, 50);
	return {
		sharedMemory: typeof SharedArrayBuffer !== "undefined" && module.HEAPU8.buffer instanceof SharedArrayBuffer,
	};
}

async function execute(message) {
	if (message.type === "open") return open(message.config);
	if (!modulePromise) throw Error("Open the worker lobby first.");
	const module = await modulePromise;
	if (message.type === "close") {
		clearInterval(timer);
		module.ccall("phoenix_worker_host_close", null, [], []);
		return true;
	}
	if (message.type === "attach") {
		if (!Array.isArray(message.archives) || !message.archives.length || message.archives.length > 32)
			throw Error("Invalid mission archive selection.");
		for (const archive of message.archives)
			if (!(archive instanceof ArrayBuffer) || !archive.byteLength || archive.byteLength > 128 * 1024 * 1024)
				throw Error("Invalid mission archive size.");
		try {
			for (const archive of message.archives) {
				const pointer = module._malloc(archive.byteLength);
				if (!pointer) throw Error("Cannot allocate mission archive buffer.");
				try {
					module.HEAPU8.set(new Uint8Array(archive), pointer);
					call(module, "archive", ["number", "number"], [pointer, archive.byteLength]);
				} finally {
					module._free(pointer);
				}
			}
			return call(module, "attach");
		} catch (error) {
			module.ccall("phoenix_worker_host_discard_archives", null, [], []);
			throw error;
		}
	}
	if (message.type === "action") return call(module, "action", ["string"], [message.action]);
	throw Error("Unknown host worker operation.");
}

let requests = Promise.resolve();
self.onmessage = ({ data }) => {
	requests = requests.then(async () => {
		try {
			self.postMessage({ type: "reply", id: data.id, ok: true, value: await execute(data) });
		} catch (error) {
			self.postMessage({ type: "reply", id: data.id, ok: false, error: String(error.message || error) });
		}
	});
};
