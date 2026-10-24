/* Authoring-only worker. Authoritative gameplay remains in the compiled C++ host. */
let runtime;
let requests = Promise.resolve();
let lastTick = 0;
let lastReport = 0;
let queuedRequests = 0;
let queuedBytes = 0;
let lastFeedback = { running: false, paused: true };
let workerError;

function report(feedback, requestSucceeded = false) {
	if (requestSucceeded) workerError = undefined;
	if (workerError) feedback = { ...feedback, ...workerError };
	lastFeedback = feedback;
	self.postMessage({ type: "feedback", feedback });
}

function fail(error) {
	const status = String(error?.message || error);
	workerError = { status, host_diagnostics: [status], worker_error: true };
	report(lastFeedback);
}

function ensureRuntime() {
	if (!runtime) {
		runtime = (async () => {
			importScripts(new URL("phoenix_workshop_test.js", self.location.href).href);
			if (typeof self.createPhoenixWorkshopTestModule !== "function") {
				throw new Error("Disposable Workshop test module is unavailable.");
			}
			const module = await self.createPhoenixWorkshopTestModule({
				locateFile: (file) => new URL(file, self.location.href).href,
				print: (message) => self.postMessage({ type: "log", message: String(message) }),
				printErr: (message) => self.postMessage({ type: "log", message: String(message) }),
			});
			lastTick = performance.now();
			setInterval(() => {
				const now = performance.now();
				const delta = Math.max(0, now - lastTick);
				const elapsed = Math.min(250, Math.floor(delta));
				lastTick = now - (delta - Math.floor(delta));
				try {
					module.ccall("phoenix_workshop_test_advance", null, ["number"], [elapsed]);
					if (now - lastReport >= 100) {
						lastReport = now;
						report(JSON.parse(module.ccall("phoenix_workshop_test_feedback", "string", [], [])));
					}
				} catch (error) {
					fail(error);
				}
			}, 50);
			return module;
		})().catch((error) => {
			runtime = undefined;
			throw error;
		});
	}
	return runtime;
}

function upload(module, buffer) {
	if (!(buffer instanceof ArrayBuffer) || !buffer.byteLength || buffer.byteLength > 64 * 1024 * 1024) {
		throw new Error("Invalid disposable test archive transfer.");
	}
	const identity = module.ccall("phoenix_workshop_test_begin_blob", "number", ["number"], [buffer.byteLength]);
	if (!identity) throw new Error("Disposable test archive exceeds the bounded worker inbox.");
	const bytes = new Uint8Array(buffer);
	const pointer = module._malloc(Math.min(bytes.length, 65536));
	if (!pointer) throw new Error("Disposable test upload buffer allocation failed.");
	try {
		for (let offset = 0; offset < bytes.length; offset += 65536) {
			const chunk = bytes.subarray(offset, Math.min(bytes.length, offset + 65536));
			// Read the current heap view after calls that can grow WASM memory.
			if (!(module.HEAPU8 instanceof Uint8Array))
				throw new Error("Disposable test module does not export HEAPU8.");
			module.HEAPU8.set(chunk, pointer);
			const copied = module.ccall(
				"phoenix_workshop_test_write_blob",
				"number",
				["number", "number", "number", "number"],
				[identity, offset, pointer, chunk.length],
			);
			if (!copied) throw new Error("Disposable test archive upload failed.");
		}
	} finally {
		module._free(pointer);
	}
	return identity;
}

self.onmessage = (event) => {
	if (event.data?.type !== "request") return;
	const incoming = event.data;
	const bytes =
		(incoming.pack instanceof ArrayBuffer ? incoming.pack.byteLength : 0) +
		(Array.isArray(incoming.dependencies)
			? incoming.dependencies.reduce((sum, pack) => sum + (pack instanceof ArrayBuffer ? pack.byteLength : 0), 0)
			: 0);
	const acknowledge = () => self.postMessage({ type: "ack", bytes });
	if (queuedRequests >= 16 || bytes > 128 * 1024 * 1024 || queuedBytes + bytes > 128 * 1024 * 1024) {
		fail(new Error("Disposable test request queue is full. Retry when the current request finishes."));
		acknowledge();
		return;
	}
	queuedRequests++;
	queuedBytes += bytes;
	requests = requests
		.then(async () => {
			const module = await ensureRuntime();
			const request = {
				action: incoming.action,
				mission: incoming.mission,
				slot: incoming.slot,
				ship: incoming.ship,
			};
			if (request.action === "start") {
				module.ccall("phoenix_workshop_test_reset_blobs", null, [], []);
				if (!Array.isArray(incoming.dependencies) || incoming.dependencies.length > 64)
					throw new Error("Invalid disposable test dependencies.");
				request.dependencies = incoming.dependencies.map((pack) => upload(module, pack));
				request.pack = upload(module, incoming.pack);
			}
			const response = module.ccall(
				"phoenix_workshop_test_request",
				"string",
				["string"],
				[JSON.stringify(request)],
			);
			const feedback = JSON.parse(response);
			report(feedback, feedback.request_succeeded === true);
		})
		.catch(fail)
		.finally(() => {
			queuedRequests--;
			queuedBytes -= bytes;
			acknowledge();
		});
};
