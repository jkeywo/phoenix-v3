/* Ordinary browser host. Only this worker owns the C++ lobby and World. */
let modulePromise;
let timer;
let presentation = false;
let framePending = false;
let acknowledgeAsset;
let closed = false;
const links = new Map();
let sequence = 0;
const encoder = new TextEncoder();

function openLink(id, config) {
	const direct = new Map(),
		pending = new Map();
	let relay = false,
		queued = 0;
	const operation = (op, peer = "", data = "", sequence = 0) =>
		self.postMessage({ type: "link-operation", id, op, peer, data, sequence });
	const send = (op, peer, data) => {
		const size = encoder.encode(data).length;
		if (queued + size > config.buffer_limit || pending.size >= 256) return false;
		const token = ++sequence;
		pending.set(token, size);
		queued += size;
		operation(op, peer, data, token);
		return true;
	};
	const state = {
		notice(kind, peer) {
			if (kind === config.events.ws_open) relay = true;
			if (kind === config.events.ws_closed) relay = false;
			if (kind === config.events.opened) direct.set(peer, 0);
			if (kind === config.events.lost) direct.delete(peer);
		},
		ack(message) {
			if (!pending.has(message.sequence)) return;
			queued -= pending.get(message.sequence);
			pending.delete(message.sequence);
			if (message.peer) state.updateBuffered(message.peer, message.accepted ? message.buffered : -1);
		},
		updateBuffered(peer, bytes) {
			if (bytes < 0) direct.delete(peer);
			else if (direct.has(peer)) direct.set(peer, bytes);
		},
		envelope: (data) => relay && send("envelope", "", data),
		buffered: (peer) => (direct.has(peer) ? direct.get(peer) + queued : -1),
		direct: (peer, data) => direct.has(peer) && send("direct", peer, data),
		initiate: (peer) => operation("initiate", peer),
		reset: (peer) => {
			direct.delete(peer);
			operation("reset", peer);
		},
		signal: (peer, data) => operation("signal", peer, data),
		close: () => {
			links.delete(id);
			operation("close");
		},
	};
	links.set(id, state);
	operation("open", "", JSON.stringify(config));
	return state;
}

function call(module, operation, types = [], values = []) {
	const result = JSON.parse(module.ccall("phoenix_worker_host_" + operation, "string", types, values));
	if (!result.ok) throw Error(result.error);
	return result.value;
}

function callJson(module, operation, value) {
	const bytes = encoder.encode(JSON.stringify(value) + "\0");
	const pointer = module._malloc(bytes.length);
	if (!pointer) throw Error("Cannot allocate host request.");
	try {
		module.HEAPU8.set(bytes, pointer);
		return call(module, operation, ["number"], [pointer]);
	} finally {
		module._free(pointer);
	}
}

async function open(config) {
	if (modulePromise) throw Error("This worker already owns a host.");
	importScripts(new URL("phoenix_browser_host.js", self.location.href).href);
	modulePromise = self.createPhoenixBrowserHost({
		locateFile: (file) => new URL(file, self.location.href).href,
		phoenixWorkerLink: config.proxy_transport ? openLink : undefined,
	});
	const module = await modulePromise;
	callJson(module, "open", config);
	presentation = config.presentation === true;
	let previous = performance.now();
	timer = setInterval(() => {
		try {
			const now = performance.now();
			const publish = presentation && !framePending;
			const frame = call(module, "frame", ["number", "number"], [(now - previous) / 1000, publish ? 1 : 0]);
			if (!presentation || publish) {
				framePending = presentation;
				self.postMessage({ type: "frame", frame });
			}
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
		closed = true;
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
			const attached = call(module, "attach");
			if (presentation)
				void sendCatalog(module).catch((error) =>
					self.postMessage({ type: "error", error: String(error.message || error) }),
				);
			return attached;
		} catch (error) {
			module.ccall("phoenix_worker_host_discard_archives", null, [], []);
			throw error;
		}
	}
	if (message.type === "action") return call(module, "action", ["string"], [message.action]);
	if (message.type === "save") {
		try {
			const size = call(module, "save");
			const pointer = module.ccall("phoenix_worker_host_save_bytes", "number", [], []);
			const bytes = module.HEAPU8.slice(pointer, pointer + size);
			self.postMessage({ type: "save", store: message.store, bytes }, [bytes.buffer]);
		} finally {
			module.ccall("phoenix_worker_host_release_save", null, [], []);
		}
		return true;
	}
	if (message.type === "command") return callJson(module, "command", message.command);
	throw Error("Unknown host worker operation.");
}

async function sendCatalog(module) {
	const catalog = call(module, "catalog");
	self.postMessage({ type: "catalog", catalog });
	for (const name of catalog.asset_names) {
		if (closed) return;
		const size = module.ccall("phoenix_worker_host_asset_size", "number", ["string"], [name]);
		const pointer = module.ccall("phoenix_worker_host_asset", "number", ["string"], [name]);
		const bytes = module.HEAPU8.slice(pointer, pointer + size);
		await new Promise((resolve) => {
			acknowledgeAsset = resolve;
			self.postMessage({ type: "asset", name, bytes }, [bytes.buffer]);
		});
	}
	if (!closed) self.postMessage({ type: "catalog-ready" });
}

let requests = Promise.resolve();
self.onmessage = ({ data }) => {
	if (data.type === "frame-ack") {
		framePending = false;
		return;
	}
	if (data.type === "asset-ack") {
		acknowledgeAsset?.();
		acknowledgeAsset = undefined;
		return;
	}
	requests = requests.then(async () => {
		try {
			if (data.type === "link-notice" || data.type === "link-ack" || data.type === "link-buffered") {
				const link = links.get(data.id);
				if (!link) return;
				if (data.type === "link-ack") link.ack(data);
				else if (data.type === "link-buffered") link.updateBuffered(data.peer, data.buffered);
				else {
					link.notice(data.kind, data.peer);
					const module = await modulePromise;
					const bytes = encoder.encode(data.data + "\0");
					const pointer = module._malloc(bytes.length);
					if (!pointer) throw Error("Cannot allocate transport notice.");
					try {
						module.HEAPU8.set(bytes, pointer);
						module.ccall(
							"phoenix_link_notice",
							null,
							["number", "number", "string", "number", "number"],
							[data.id, data.kind, data.peer, pointer, 0],
						);
					} finally {
						module._free(pointer);
					}
				}
				return;
			}
			self.postMessage({ type: "reply", id: data.id, ok: true, value: await execute(data) });
		} catch (error) {
			self.postMessage({ type: "reply", id: data.id, ok: false, error: String(error.message || error) });
		} finally {
			if (data.type === "link-notice" && data.token)
				self.postMessage({ type: "link-notice-ack", id: data.id, token: data.token });
		}
	});
};
